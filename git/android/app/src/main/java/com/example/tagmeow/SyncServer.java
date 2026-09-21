package com.example.tagmeow;

import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

// 同步服务端（分享端）

// 一对一连接模式（与 SyncClient 对齐）：
// 1. start() 启动工作线程 无客户端时：周期广播一次消息 + 轮询接受连接（两者在同一线程交替执行
//    保证 stop() 可打断）
// 2. 客户端连接成功：停止广播 从队列取目录逐个发送其中所有文件
//    每个文件：发送文件头 -> 等待客户端回复 1 字节（'1' = 发送数据 '0' = 跳过）
//    会话期间 enqueueDirectory() 推入的目录会继续按序发送
// 3. 队列为空：等待 empty_queue_wait 分钟（期间有新目录继续发送）超时仍为空才断开客户端
//    清空队列并重新开始广播
// 4. stop()：停止工作线程并清理

// 与 core 的一处 Android 适配：Java 的 Socket 没有可移植的写超时 所以 stop()/disconnect()
// 除了置标志之外还会主动 interrupt()/close() 连接 用「别的线程关套接字」来打断阻塞 I/O
// 保证 join 一定有界返回

public final class SyncServer {

    // 工作线程回调：会话过程中的状态变化（在工作线程上执行 不要在回调里做 UI 操作）
    @FunctionalInterface
    public interface SessionCallback {

        // success=true  error=NO_MESSAGE_AVAILABLE —— 会话中发送队列为空 上层可趁等待期继续入队
        // success=true  error=NONE                 —— 会话正常结束（等待超时后断开客户端）
        // success=false error=其他                 —— 会话出错/被中断
        void onSession(boolean success, SyncError error);
    }

    // 任务(入队目录)级完成回调：一个目录下的文件全部发送完毕时在工作线程调用
    // core: SyncServer::setTaskCallback(std::function<void(const TaskReport &)>)
    @FunctionalInterface
    public interface TaskCallback {

        void onTask(SyncBasic.TaskReport report);
    }

    // accept 轮询粒度（core: workerLoop 里 sleepMillis(400)）
    private static final int ACCEPT_POLL_MS = 400;

    // 传输级阻塞 I/O 的有界超时（core: setTimeouts(30000, 30000)）
    private static final int SERVER_SEND_TIMEOUT_MS = 30000;
    private static final int SERVER_RECV_TIMEOUT_MS = 30000;

    // 「会话结束」控制帧的确认等待上限（core: waitForSessionEndAck 里 5 秒 deadline + 500ms 轮询）
    private static final int SESSION_END_ACK_TIMEOUT_MS = 5000;

    // stop() 等待工作线程退出的上限
    private static final long STOP_JOIN_TIMEOUT_MS = 5000L;

    private final String magic_word;
    private final int udp_port;
    private final long empty_queue_wait_ms;

    // 待发送目录队列（core: task_queue_ + queue_mutex_ + cv_）
    private final ReentrantLock queue_lock = new ReentrantLock();
    private final Condition queue_cv = queue_lock.newCondition();
    private final ArrayDeque<File> task_queue = new ArrayDeque<>();

    private ServerSocket acceptor;
    private BroadcastSender broadcaster;
    private volatile TcpConnection client;

    private String server_name = "";
    private String advertise_ip = "";
    private int server_port = 0;

    private Thread worker_thread;
    private volatile boolean running = false;
    private volatile boolean client_connected = false;
    private volatile boolean broadcasting = false;
    private volatile boolean started = false;
    private volatile boolean disconnect_requested = false;

    private volatile String error_string = "";

    // 本次会话已真正发出去 / 被对端跳过的文件数（core 只按会话回调 没有这些计数）
    // 纯 Android 端为了让 UI 说清楚「发了多少」才维护的 不参与协议
    private volatile int sent_file_count = 0;
    private volatile int skipped_file_count = 0;
    private volatile long sent_byte_count = 0L;

    // 任务级完成通知回调（主线程设置 工作线程调用 同一把锁保护 与 core 的 task_mutex_ 一致）
    private final Object task_lock = new Object();
    private TaskCallback task_callback = null;

    // empty_queue_wait_minutes：会话中队列为空时等待新目录入队的时长（core 用 std::chrono::minutes）
    public SyncServer(int port, String magic_word, int empty_queue_wait_minutes) {

        this.udp_port = port;
        this.magic_word = magic_word == null ? SyncBasic.UDP_DEFAULT_MAGIC : magic_word;
        this.empty_queue_wait_ms = Math.max(empty_queue_wait_minutes, 0) * 60_000L;
    }

    // 启动工作线程：绑定 TCP 端口（port == 0 时由系统分配 广播广告实际端口）
    // 返回 NONE 表示启动成功
    public SyncError start(String server_name, int port, SessionCallback callback) {

        if (started) {
            setError("[tip] the server has started");

            return SyncError.OPERATION_IN_PROGRESS;
        }

        try {
            acceptor = new ServerSocket();
            acceptor.setReuseAddress(true);
            acceptor.bind(new InetSocketAddress(port));
            // 用 accept 超时代替 core 的非阻塞 accept + sleep：同样是轮询 同样能被 stop() 打断
            acceptor.setSoTimeout(ACCEPT_POLL_MS);
        } catch (IOException error) {
            setError("[warning] failed to bind the port: " + error);
            closeAcceptor();

            return SyncException.errorOf(error);
        }

        server_port = acceptor.getLocalPort();

        // 解析本机接口：广告 IP 与子网定向广播地址
        // （多网卡/VPN 环境自动解析错误时可 setAdvertiseIP 覆盖 须在 start() 前调用）
        String broadcast_addr = "255.255.255.255";

        SyncBasic.InterfaceInfo info = SyncBasic.findInterfaceInfo();

        if (info != null) {
            broadcast_addr = info.broadcast;

            if (advertise_ip.isEmpty()) {
                advertise_ip = info.ip;
            }
        }

        this.server_name = server_name == null || server_name.isEmpty() ? "tagmeow" : server_name;

        try {
            broadcaster = new BroadcastSender(udp_port, broadcast_addr);
        } catch (SyncException error) {
            setError("[warning] failed to create the broadcaster: " + error.getMessage());
            closeAcceptor();

            return error.getError();
        }

        running = true;
        broadcasting = true;
        started = true;

        worker_thread = new Thread(() -> workerLoop(callback), "tagmeow-sync-server");
        worker_thread.setDaemon(true);
        worker_thread.start();

        error_string = "";

        return SyncError.NONE;
    }

    // 停止工作线程并清理
    public SyncError stop() {

        if (!started) {
            return SyncError.NONE;
        }

        running = false;
        signalQueue();

        TcpConnection connection = client;

        if (connection != null) {
            connection.interrupt();
        }

        Thread thread = worker_thread;

        // 防止在回调（工作线程）内调用 stop 时自 join 死锁
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(STOP_JOIN_TIMEOUT_MS);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
            }
        }

        worker_thread = null;
        started = false;

        closeAcceptor();

        if (broadcaster != null) {
            broadcaster.close();
            broadcaster = null;
        }

        TcpConnection remaining = client;

        if (remaining != null) {
            remaining.close();
            client = null;
        }

        client_connected = false;

        queue_lock.lock();

        try {
            task_queue.clear();
        } finally {
            queue_lock.unlock();
        }

        return SyncError.NONE;
    }

    // 选择要发送的目录 推入队列（客户端自行按下载记录校验进度）
    public void enqueueDirectory(File dir) {

        if (dir == null) {
            return;
        }

        queue_lock.lock();

        try {
            task_queue.addLast(dir);
        } finally {
            queue_lock.unlock();
        }

        signalQueue();
    }

    // 获取待发送目录队列的快照（返回拷贝 可安全迭代）
    public List<File> getTaskQueue() {

        queue_lock.lock();

        try {
            return new ArrayList<>(task_queue);
        } finally {
            queue_lock.unlock();
        }
    }

    // 断开当前客户端并清空队列（随后重新开始广播）
    // 置位断开请求并唤醒工作线程 由工作线程在下一个有界阻塞点完成断开
    public SyncError disconnect() {

        disconnect_requested = true;
        signalQueue();

        TcpConnection connection = client;

        if (connection != null) {
            connection.interrupt();
        }

        return SyncError.NONE;
    }

    // 任务级完成通知：一个入队目录下的所有文件发送完毕时在工作线程回调
    // 上层需自行保证线程安全（Android 侧用 runOnUiThread 回到主线程） 建议在 start() 之前设置
    public void setTaskCallback(TaskCallback callback) {

        synchronized (task_lock) {
            task_callback = callback;
        }
    }

    // 拷贝回调后在锁外调用（不持锁执行上层代码）
    private void notifyTask(SyncBasic.TaskReport report) {

        TaskCallback callback;

        synchronized (task_lock) {
            callback = task_callback;
        }

        if (callback == null) {
            return;
        }

        try {
            callback.onTask(report);
        } catch (Throwable error) {
            // 回调里抛异常不能把工作线程带走
            setError("[warning] task callback failed: " + error);
        }
    }

    // 手动指定对外广告的 IP（默认 start 时按本机首个可用接口自动解析）
    public void setAdvertiseIP(String ip) {

        advertise_ip = ip == null ? "" : ip;
    }

    public boolean isStarted() {
        return started;
    }

    public boolean isClientConnected() {
        return client_connected;
    }

    public int getServerPort() {
        return server_port;
    }

    public String getAdvertiseIP() {
        return advertise_ip;
    }

    public String getLastError() {
        return error_string;
    }

    // 本次会话已经发出去的文件数（会话开始时清零）
    public int getSentFileCount() {
        return sent_file_count;
    }

    // 本次会话被对端跳过（已有下载记录）的文件数
    public int getSkippedFileCount() {
        return skipped_file_count;
    }

    // 本次会话已经发出去的字节数
    public long getSentBytes() {
        return sent_byte_count;
    }

    // 工作线程主循环（状态机：广播 + 轮询 accept <-> 会话发送）
    private void workerLoop(SessionCallback callback) {

        while (running) {
            if (client_connected) {
                // 会话模式：按序发送队列中的目录

                // 上层请求断开：立即结束会话（回调说明被中断）
                if (disconnect_requested) {
                    disconnect_requested = false;
                    notifySession(callback, false, SyncError.OPERATION_CANCELED);
                    closeClient();

                    continue;
                }

                File dir = pollQueue();

                if (dir == null) {
                    // 队列为空：通知上层「发送队列为空」 上层可趁等待期继续入队（回调在锁外执行）
                    notifySession(callback, true, SyncError.NO_MESSAGE_AVAILABLE);

                    // 等待新目录入队（最长 empty_queue_wait）期间有新目录则继续发送
                    // 超时仍为空或被停止/请求断开则结束会话
                    dir = awaitQueue(empty_queue_wait_ms);
                }

                if (dir != null) {
                    try {
                        sendDirectory(dir);
                    } catch (SyncException error) {
                        notifySession(callback, false, error.getError());
                        closeClient();

                        continue;
                    }

                    continue;
                }

                // 队列为空且等待超时：会话正常结束 被停止或请求断开则视为中断
                if (running && !disconnect_requested) {
                    // 先显式发送「会话结束」控制帧并等客户端确认 才算正常结束
                    // （TCP EOF 无法区分正常断开与网络中断 故不以其作为正常结束依据）
                    SyncError end_error = SyncError.NONE;

                    TcpConnection connection = client;

                    if (connection == null) {
                        end_error = SyncError.NOT_CONNECTED;
                    } else {
                        try {
                            connection.sendSessionEnd();
                            waitForSessionEndAck(connection);
                        } catch (SyncException failure) {
                            end_error = failure.getError();
                        }
                    }

                    notifySession(callback, !end_error.isError(), end_error);
                } else {
                    notifySession(callback, false, SyncError.OPERATION_CANCELED);
                }

                disconnect_requested = false; // 消费断开请求（等待被断开请求唤醒时）
                closeClient();

                continue;
            }

            // 无客户端：周期广播 + 轮询 accept

            if (disconnect_requested) {
                disconnect_requested = false; // 无客户端时断开请求为空操作 丢弃残留标志
            }

            if (broadcaster != null && broadcasting) {
                SyncError error = broadcaster.send(buildMessage());

                if (error.isError()) {
                    setError(broadcaster.getLastError());
                }
            }

            int accepted = tryAccept();

            if (accepted > 0) {
                client_connected = true;
                broadcasting = false;

                continue;
            }

            // accepted == 0 时 accept 本身已经等过 ACCEPT_POLL_MS（等价 core 的 sleepMillis）
            // 只有套接字出错才补一次休眠 免得忙等
            if (accepted < 0) {
                sleepMillis(ACCEPT_POLL_MS);
            }
        }

        // 工作线程退出：若会话被中断则通知回调
        if (callback != null && client_connected) {
            notifySession(callback, false, SyncError.OPERATION_CANCELED);
        }

        closeClient();
    }

    // 广播一次消息（单次 循环由 workerLoop 控制）
    private SyncBasic.UDPMessage buildMessage() {

        SyncBasic.UDPMessage msg = new SyncBasic.UDPMessage();
        msg.name = server_name;
        msg.ip = advertise_ip.isEmpty() ? SyncBasic.localIp() : advertise_ip;
        msg.port = server_port;
        msg.magic_word = magic_word;

        return msg;
    }

    // 尝试接受一个客户端连接
    // 返回 1 = 已接受 0 = 轮询超时（没有连接 -1 = 套接字错误
    private int tryAccept() {

        ServerSocket server = acceptor;

        if (server == null || server.isClosed()) {
            setError("[warning] the listening socket is closed");

            return -1;
        }

        Socket socket;

        try {
            socket = server.accept();
        } catch (SocketTimeoutException poll) {
            return 0; // 没有连接
        } catch (IOException error) {
            if (!running) {
                return -1; // stop() 关掉了监听套接字 不是错误
            }

            if (server.isClosed()) {
                return -1;
            }

            setError("[warning] failed to accept connection: " + error);

            return -1;
        }

        try {
            TcpConnection connection = new TcpConnection(socket);
            connection.setTimeouts(SERVER_SEND_TIMEOUT_MS, SERVER_RECV_TIMEOUT_MS);

            // 新会话开始：进度计数清零
            sent_file_count = 0;
            skipped_file_count = 0;
            sent_byte_count = 0L;

            client = connection;

            return 1;
        } catch (SyncException error) {
            setError("[warning] failed to set up the connection: " + error.getMessage());
            closeQuietly(socket);

            return -1;
        }
    }

    // 发送一个目录：每个文件 发送文件头 -> 等待客户端回复 -> 决定是否发送数据
    private void sendDirectory(File dir) throws SyncException {

        if (!dir.isDirectory()) {
            throw new SyncException(SyncError.NOT_A_DIRECTORY, dir.getAbsolutePath());
        }

        List<File> files = new ArrayList<>();
        collectFiles(dir, files);

        // 排序保证发送顺序确定（core: std::sort）
        Collections.sort(files, Comparator.comparing(File::getAbsolutePath));

        TcpConnection connection = client;

        if (connection == null) {
            throw new SyncException(SyncError.NOT_CONNECTED);
        }

        // 任务(入队目录)级统计：文件数固定 字节数按实际发送量累加（客户端跳过的不计）
        SyncBasic.TaskReport report = new SyncBasic.TaskReport();
        report.name = dir.getName();
        report.file_count = files.size();
        report.byte_count = 0L;

        for (int file_index = 0; file_index < files.size(); file_index++) {
            File file = files.get(file_index);

            if (!running || disconnect_requested) {
                throw new SyncException(SyncError.OPERATION_CANCELED);
            }

            SyncBasic.FileHeader header = new SyncBasic.FileHeader();
            header.parent_dir = parentDirOf(dir, file);
            header.file_name = file.getName();
            header.file_size = file.length();
            // 任务(目录)的最后一个文件打标记：接收端据此给出任务完成提示
            header.last_in_dir = (file_index + 1 == files.size());

            connection.sendHeader(header);

            int reply = waitForClientReply(connection);

            if (reply == '0') {
                skipped_file_count++;

                continue; // 客户端已有成功下载记录 跳过
            }

            // 发送文件数据：严格按 header.file_size 字节发送
            long offset = 0;

            while (offset < header.file_size) {
                if (!running || disconnect_requested) {
                    throw new SyncException(SyncError.OPERATION_CANCELED);
                }

                int sent = connection.sendFileData(file, offset, SyncBasic.FILE_CHUNK_SIZE);

                if (sent == 0) {
                    // 文件在发送过程中被截断：中止会话（客户端不会死等）
                    throw new SyncException(SyncError.IO_ERROR, "file truncated: " + file.getAbsolutePath());
                }

                offset += sent;
            }

            report.byte_count += header.file_size; // 实际发送量（客户端跳过的不计）

            sent_file_count++;
            sent_byte_count += header.file_size;
        }

        // 任务(入队目录)全部文件处理完毕：通知上层（会话级回调语义不变）
        notifyTask(report);
    }

    // 等待客户端回复一个字节
    // 返回 '1'（发送）或 '0'（跳过）；其他值视为协议错误
    private int waitForClientReply(TcpConnection connection) throws SyncException {

        while (running && !disconnect_requested) {
            int value = connection.receiveByte(SyncBasic.REPLY_POLL_MS);

            if (value < 0) {
                continue; // 未收到回复 继续等待（可被 running / disconnect_requested 打断）
            }

            if (value == '1' || value == '0') {
                return value;
            }

            throw new SyncException(SyncError.PROTOCOL_ERROR, "unexpected reply byte: " + value);
        }

        throw new SyncException(SyncError.OPERATION_CANCELED);
    }

    // 等待客户端对「会话结束」控制帧的确认（有界 可被 stop()/disconnect() 打断）
    // 客户端回 '1' 才算确认 其他字节视为协议错误 5 秒内没等到按超时处理
    private void waitForSessionEndAck(TcpConnection connection) throws SyncException {

        long dead_line = System.currentTimeMillis() + SESSION_END_ACK_TIMEOUT_MS;

        while (running && !disconnect_requested) {
            int value = connection.receiveByte(SyncBasic.REPLY_POLL_MS);

            if (value == '1') {
                return;
            }

            if (value >= 0) {
                throw new SyncException(SyncError.PROTOCOL_ERROR, "unexpected session-end ack: " + value);
            }

            if (System.currentTimeMillis() >= dead_line) {
                throw new SyncException(SyncError.TIMED_OUT, "session-end ack timeout");
            }
        }

        throw new SyncException(SyncError.OPERATION_CANCELED);
    }

    // 断开并清理客户端连接（workerLoop / stop 共用）
    private void closeClient() {

        TcpConnection connection = client;
        client = null;

        if (connection != null) {
            connection.close();
        }

        client_connected = false;
        broadcasting = true; // 会话结束（正常/断开/出错）后恢复广播 便于重新被发现

        // 清空待发送队列
        queue_lock.lock();

        try {
            task_queue.clear();
        } finally {
            queue_lock.unlock();
        }
    }

    // 非阻塞取一个待发送目录
    private File pollQueue() {

        queue_lock.lock();

        try {
            return task_queue.pollFirst();
        } finally {
            queue_lock.unlock();
        }
    }

    // 等待新目录入队（最长 wait_ms）被停止/请求断开也会提前返回
    private File awaitQueue(long wait_ms) {

        queue_lock.lock();

        try {
            long dead_line = System.nanoTime() + wait_ms * 1_000_000L;

            while (running && !disconnect_requested && task_queue.isEmpty()) {
                long remaining = dead_line - System.nanoTime();

                if (remaining <= 0) {
                    break;
                }

                try {
                    queue_cv.awaitNanos(remaining);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();

                    break;
                }
            }

            if (running && !disconnect_requested && !task_queue.isEmpty()) {
                return task_queue.pollFirst();
            }

            return null;
        } finally {
            queue_lock.unlock();
        }
    }

    private void signalQueue() {

        queue_lock.lock();

        try {
            queue_cv.signalAll();
        } finally {
            queue_lock.unlock();
        }
    }

    // 计算相对父目录：以入队目录自身的名字为首段
    // 避免多个入队目录下同名文件在客户端相互覆盖（根目录文件 = 入队目录名）
    static String parentDirOf(File root, File file) {

        String base = root.getName() == null ? "" : root.getName();

        File parent = file.getParentFile();

        if (parent == null) {
            return base;
        }

        String relative;

        try {
            relative = root.getAbsoluteFile().toPath().relativize(parent.getAbsoluteFile().toPath()).toString().replace('\\', '/');
        } catch (IllegalArgumentException error) {
            // 不在同一个根下（理论上不会发生）退回只用目录名
            return base;
        }

        if (relative.isEmpty() || ".".equals(relative)) {
            return base;
        }

        return base.isEmpty() ? relative : base + "/" + relative;
    }

    // 递归收集目录下所有普通文件 读不出来的子目录跳过
    // 不跟进目录符号链接 避免自指目录把递归拖死
    static void collectFiles(File root, List<File> out) {

        File[] children = root.listFiles();

        if (children == null) {
            return;
        }

        for (File child : children) {
            if (child.isDirectory()) {
                if (isSymbolicLink(child)) {
                    continue;
                }

                collectFiles(child, out);
            } else if (child.isFile()) {
                out.add(child);
            }
        }
    }

    private static boolean isSymbolicLink(File file) {

        try {
            return java.nio.file.Files.isSymbolicLink(file.toPath());
        } catch (RuntimeException error) {
            return false;
        }
    }

    private void notifySession(SessionCallback callback, boolean success, SyncError error) {

        if (callback == null) {
            return;
        }

        try {
            callback.onSession(success, error);
        } catch (Throwable error_in_callback) {
            // 回调里抛异常不能把工作线程带走
            setError("[warning] session callback failed: " + error_in_callback);
        }
    }

    private void setError(String message) {

        error_string = message;
    }

    private void closeAcceptor() {

        ServerSocket server = acceptor;
        acceptor = null;

        if (server == null) {
            return;
        }

        try {
            server.close();
        } catch (IOException error) {
            // 关闭失败无所谓
        }
    }

    private static void closeQuietly(Socket socket) {

        if (socket == null) {
            return;
        }

        try {
            socket.close();
        } catch (IOException error) {
            // 关闭失败无所谓
        }
    }

    private static void sleepMillis(long ms) {

        if (ms <= 0) {
            return;
        }

        try {
            Thread.sleep(ms);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
        }
    }
}
