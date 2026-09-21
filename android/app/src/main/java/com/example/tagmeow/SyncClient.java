package com.example.tagmeow;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

// 同步客户端（下载端）

// 工作流程（与 SyncServer 对齐）：
// 1. 上层调用 scanServers() 同步扫描局域网 阻塞返回去重后的服务器列表
// 2. 调用 startDownload(server_index, cb) 发起一次下载会话（异步立即返回）
//    连接服务器后客户端不再需要上层操作 会话内被动接收 结束/失败/断开时回调
//    每个文件：接收文件头 -> 与本地「成功下载记录」比对：
//        无记录：发送 1 字节 '1' 通知服务器发送文件 接收文件数据 成功后写入记录
//        已有成功记录 发送 1 字节 '0' 通知服务器跳过该文件
//    特殊情况 服务器发送队列为空时由服务器主动断开连接
//    回调以 success=true、error=NO_MESSAGE_AVAILABLE 提示上层「发送队列为空」（本次无文件可下载）

// 默认下载路径构造时固定（与 core 一致） 下载记录持久化到 download_path/records.json
// 想换下载目录就重新 new 一个 SyncClient（SyncEngine 负责这件事）

public final class SyncClient {

    // 下载会话回调（在工作线程上执行 不要在回调里做 UI 操作）
    @FunctionalInterface
    public interface DownloadCallback {

        // success=true  error=NONE                 —— 至少完成 1 个文件 且收到服务器「会话结束」标记后正常完成
        // success=true  error=NO_MESSAGE_AVAILABLE —— 服务器发送队列为空 本次无文件可下载（正常完成）
        // success=false error=其他                 —— 连接失败/中途中断（含未收到会话结束标记的 EOF）/协议错误
        void onFinished(boolean success, SyncError error);
    }

    // 任务(入队目录)级完成回调：一个目录下的文件全部处理完（接收或跳过）时在工作线程调用
    // core: SyncClient::setTaskCallback(std::function<void(const TaskReport &)>)
    @FunctionalInterface
    public interface TaskCallback {

        void onTask(SyncBasic.TaskReport report);
    }

    // 成功下载记录（身份 = parent_dir + file_name + file_size）
    // core: struct DownloadRecord
    public static final class DownloadRecord {

        public String parent_dir = "";
        public String file_name = "";
        public long file_size = 0L;
    }

    // 连接超时（core: connectWithTimeout 5000ms）
    private static final int CONNECT_TIMEOUT_MS = 5000;

    // 客户端套接字超时（core: setTimeouts(30000, 500)）
    // 接收 500ms 是轮询粒度：空闲（服务器空队列等待）由 readFully 继续等待不报错，
    // disconnect() 也能在一个轮询周期内打断
    private static final int CLIENT_SEND_TIMEOUT_MS = 30000;
    private static final int CLIENT_RECV_TIMEOUT_MS = 500;

    // close() 等待工作线程退出的上限
    private static final long CLOSE_WAIT_MS = 2000L;
    private static final String ILLEGAL_NAME_CHARS = "/\\:*?\"<>|";

    private final Context context;
    private final int port;
    private final String magic_word;
    private final File download_path;
    private final File records_path;

    private final BroadcastReceiver receiver;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean is_busy = new AtomicBoolean(false);

    private volatile boolean disconnect_requested = false;
    private volatile TcpConnection connection;

    // servers_ 与 download_records_ 各自有锁（core 用 records_mutex_）
    private final Object servers_lock = new Object();
    private final List<SyncBasic.ServerInfo> servers = new ArrayList<>();

    private final Object records_lock = new Object();
    private final List<DownloadRecord> download_records = new ArrayList<>();

    private volatile String error_string = "";

    // 本次会话已经收完的文件数 / 字节数（core 只回调成功与否 没有这些计数）
    private volatile int received_file_count = 0;
    private volatile long received_byte_count = 0L;

    // 任务级完成通知回调（主线程设置 工作线程调用 同一把锁保护 与 core 的 task_mutex_ 一致）
    private final Object task_lock = new Object();
    private TaskCallback task_callback = null;

    private ExecutorService worker;

    // 构造函数即创建 BroadcastReceiver 并绑定 UDP_DEFAULT_PORT（与 core 一致）
    // 同一个端口被多实例重复占用时后启动的实例收不到广播 这是 core 里已注明的设计限制
    public SyncClient(Context context, int port, String magic_word, File download_path) throws SyncException {

        this.context = context == null ? null : context.getApplicationContext();
        this.port = port;
        this.magic_word = magic_word == null ? SyncBasic.UDP_DEFAULT_MAGIC : magic_word;
        this.download_path = download_path;
        this.records_path = new File(download_path, "records.json");

        this.receiver = new BroadcastReceiver(this.context, port, this.magic_word, SyncBasic.DEFAULT_QUIET_TIMEOUT_MS);

        SyncError error = loadRecords();

        if (error.isError()) {
            setError("[tip] failed to load the download cache: " + records_path.getAbsolutePath());
        }

        running.set(true);

        worker = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "tagmeow-sync-client");
            thread.setDaemon(true);

            return thread;
        });
    }

    // 同步扫描局域网服务器 阻塞返回去重后的服务器列表
    // num_attempts：扫描轮数（每轮内部有总超时） 结果跨轮合并去重 1 为单轮
    // 注意：下载进行中不要调用本函数（servers 会被替换）
    public List<SyncBasic.ServerInfo> scanServers(int num_attempts) {

        List<SyncBasic.ServerInfo> result = new ArrayList<>();
        Set<String> seen = new LinkedHashSet<>();

        int attempts = num_attempts <= 0 ? 1 : num_attempts;

        for (int i = 0; i < attempts; i++) {
            List<SyncBasic.ServerInfo> found = receiver.scan(SyncBasic.MAX_SERVERS, SyncBasic.SCAN_TOTAL_TIMEOUT_MS);

            if (!receiver.getLastError().isEmpty()) {
                setError("[warning] download failed: " + receiver.getLastError());

                break;
            }

            for (SyncBasic.ServerInfo server : found) {
                if (seen.add(server.key())) {
                    result.add(server);
                }
            }
        }

        synchronized (servers_lock) {
            servers.clear();
            servers.addAll(result);
        }

        return result;
    }

    // 异步下载会话：立即返回 会话结束（下载完成/服务器队列为空/失败）时在工作线程回调
    // - isDownloading() 为 true 时重复调用：立即回调失败（OPERATION_IN_PROGRESS）
    // - server_index 越界：立即回调失败（RESULT_OUT_OF_RANGE）
    public void startDownload(int server_index, DownloadCallback callback) {

        if (!running.get()) {
            notifyDownload(callback, false, SyncError.OPERATION_CANCELED);

            return;
        }

        if (!is_busy.compareAndSet(false, true)) {
            notifyDownload(callback, false, SyncError.OPERATION_IN_PROGRESS);

            return;
        }

        if (server_index < 0 || server_index >= serverCount()) {
            is_busy.set(false);
            notifyDownload(callback, false, SyncError.RESULT_OUT_OF_RANGE);

            return;
        }

        try {
            worker.execute(() -> doDownload(server_index, callback));
        } catch (RejectedExecutionException error) {
            is_busy.set(false);
            notifyDownload(callback, false, SyncError.OPERATION_CANCELED);
        }
    }

    // 断开当前连接并终止下载会话（工作线程在下一个有界阻塞点退出并回调失败）
    // 会话级断开：仅中断当前下载会话 之后可再次扫描/下载
    public void disconnect() {

        disconnect_requested = true;

        TcpConnection current = connection;

        if (current != null) {
            current.interrupt();
        }
    }

    // 清除下载缓存记录（records.json）：下次下载将重新下载全部文件（不删除已下载的文件本身）
    public void clearDownloadRecords() {

        synchronized (records_lock) {
            download_records.clear();
        }

        if (records_path.exists() && !records_path.delete()) {
            setError("[warning] failed to clear the download cache: " + records_path.getAbsolutePath());
        }
    }

    // 已经成功下载过的文件数
    public int getDownloadRecordCount() {

        synchronized (records_lock) {
            return download_records.size();
        }
    }

    public boolean isDownloading() {
        return is_busy.get();
    }

    public File getDownloadPath() {
        return download_path;
    }

    public int getPort() {
        return port;
    }

    public String getMagicWord() {
        return magic_word;
    }

    // 线程安全快照
    public List<SyncBasic.ServerInfo> getServers() {

        synchronized (servers_lock) {
            return new ArrayList<>(servers);
        }
    }

    public String getLastError() {
        return error_string;
    }

    // 本次会话已经收完的文件数（会话开始时清零）
    public int getReceivedFileCount() {
        return received_file_count;
    }

    // 本次会话已经收完的字节数
    public long getReceivedBytes() {
        return received_byte_count;
    }

    // 释放：停止工作线程与接收器
    // 能立刻打断阻塞中的 read/write 保证不用干等接收轮询周期
    public void close() {

        running.set(false);
        disconnect_requested = true;

        TcpConnection current = connection;

        if (current != null) {
            current.close();
        }

        ExecutorService executor = worker;
        worker = null;

        if (executor != null) {
            executor.shutdownNow();

            try {
                executor.awaitTermination(CLOSE_WAIT_MS, TimeUnit.MILLISECONDS);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
            }
        }

        connection = null;
        receiver.close();
    }

    // 执行下载会话（在工作线程中运行）
    private void doDownload(int index, DownloadCallback callback) {

        SyncError error = SyncError.NONE;
        boolean success = false;

        disconnect_requested = false; // 新下载会话：消费可能残留的断开请求（断开后再次下载可正常进行）

        received_file_count = 0;
        received_byte_count = 0L;

        try {
            SyncBasic.ServerInfo server = serverAt(index);

            if (server == null) {
                error = SyncError.RESULT_OUT_OF_RANGE;
            } else {
                TcpConnection connection = syncConnect(server);

                error = syncReceiveAll(connection);

                // 成功时 error 可能是 NONE（完成 ≥1 文件）或 NO_MESSAGE_AVAILABLE（服务器队列为空）
                success = error == SyncError.NONE || error == SyncError.NO_MESSAGE_AVAILABLE;
            }
        } catch (SyncException failure) {
            error = failure.getError();
            setError("[warning] download failed: " + failure.getMessage());
        } catch (Throwable failure) {
            error = SyncError.IO_ERROR;
            setError("[warning] download exception: " + failure);
        }

        closeConnection();

        is_busy.set(false);

        notifyDownload(callback, success, error);
    }

    // 连接服务器（同步）
    private TcpConnection syncConnect(SyncBasic.ServerInfo server) throws SyncException {

        closeConnection();

        Socket socket = new Socket();

        try {
            // connect(addr, timeout) 就是 core 里 connectWithTimeout 的等价物
            socket.connect(new InetSocketAddress(server.ip, server.port), CONNECT_TIMEOUT_MS);
        } catch (SocketTimeoutException timeout) {
            throw new SyncException(SyncError.TIMED_OUT, server.ip + ":" + server.port, timeout);
        } catch (IOException error) {
            throw new SyncException(SyncException.errorOf(error), server.ip + ":" + server.port, error);
        }

        TcpConnection connection = new TcpConnection(socket);
        connection.setTimeouts(CLIENT_SEND_TIMEOUT_MS, CLIENT_RECV_TIMEOUT_MS);

        this.connection = connection;

        return connection;
    }

    // 接收整个会话：文件头 -> 比对记录 -> 回复 -> 接收数据 循环
    // 会话正常结束的判定（core 部分6 起）：必须收到服务端的「会话结束」控制帧
    // TCP EOF 不再等于正常结束 —— 网络中断同样表现为 EOF 所以两者不能混为一谈
    // 只有收到控制帧才算成功 EOF 按 CONNECTION_ABORTED 失败上报
    private SyncError syncReceiveAll(TcpConnection connection) {

        int received_count = 0;

        // 当前任务(服务端一个入队目录)的统计：已处理文件数 / 已接收字节数
        int task_files = 0;
        long task_bytes = 0L;

        while (running.get() && !disconnect_requested) {
            SyncBasic.FileHeader header;

            try {
                header = connection.receiveHeader();
            } catch (SyncException failure) {
                if (failure.getError() == SyncError.EOF) {
                    // 不能用 TCP EOF 判定「服务器正常结束」：网络中断同样是 EOF
                    // 正常结束必须收到服务端的「会话结束」控制帧（见下面的 header.session_end 分支）
                    setError("[warning] download failed: connection closed without session-end marker (interrupted)");

                    return SyncError.CONNECTION_ABORTED;
                }

                setError("[warning] download failed: receive file header failed: " + failure.getMessage());

                return failure.getError();
            }

            if (header.session_end) {
                // 服务端显式声明本次会话正常结束：回一个确认字节（服务端据此确认收尾成功）
                // 数据已完整收到 确认发送失败只影响服务端判定 客户端仍按正常完成处理
                try {
                    sendReply(connection, true);
                } catch (SyncException ack_failure) {
                    setError("[warning] download done but session-end ack failed: " + ack_failure.getMessage());
                }

                if (received_count > 0) {
                    return SyncError.NONE;
                }

                // 无文件可下载（服务器发送队列为空）：正常完成 用错误码提示上层
                return SyncError.NO_MESSAGE_AVAILABLE;
            }

            if (header.file_name.isEmpty()) {
                setError("[warning] download failed: invalid file header (empty file name)");

                return SyncError.PROTOCOL_ERROR;
            }

            if (isFileAlreadyExists(header)) {
                try {
                    sendReply(connection, false); // '0' 跳过
                } catch (SyncException failure) {
                    setError("[warning] download failed: skip reply send error: " + failure.getMessage());

                    return failure.getError();
                }

                task_files++; // 跳过的文件也属于该任务（字节按 0 计）
            } else {
                try {
                    sendReply(connection, true); // '1' 发送文件数据

                    File save_path = buildSavePath(download_path, header.parent_dir, header.file_name);

                    // 进度对象：失败时也能拿到已经写进 .part 的字节数 好拼出具体错误
                    TcpConnection.TransferProgress progress = new TcpConnection.TransferProgress();

                    connection.receiveFileTo(save_path, header.file_size, progress);

                    addRecord(header);

                    SyncError save_error = saveRecords();

                    if (save_error.isError()) {
                        setError("[warning] download record save failed: " + records_path.getAbsolutePath());
                    }

                    received_file_count++;
                    received_byte_count += progress.getBytesReceived();

                    received_count++;
                    task_files++;
                    task_bytes += progress.getBytesReceived();
                } catch (SyncException failure) {
                    // 具体错误信息给上层：哪个文件、收了多少 / 共多少、失败原因
                    // 未传完的临时文件（save_path + ".part"）已由 receiveFileTo 删除 不留残留
                    setError("[warning] download failed: " + header.parent_dir + "/" + header.file_name + " (" + failure.getMessage() + ")");

                    return failure.getError();
                }
            }

            // 服务端标记的任务(目录)最后一个文件处理完毕：任务完成通知（会话级成功/失败提示不变）
            if (header.last_in_dir) {
                SyncBasic.TaskReport report = new SyncBasic.TaskReport();
                report.name = firstDirComponent(header.parent_dir);
                report.file_count = task_files;
                report.byte_count = task_bytes;

                notifyTask(report);

                task_files = 0;
                task_bytes = 0L;
            }
        }

        setError("[warning] download aborted: operation canceled");

        return SyncError.OPERATION_CANCELED;
    }

    private boolean isFileAlreadyExists(SyncBasic.FileHeader header) {

        boolean recorded = false;

        synchronized (records_lock) {
            for (DownloadRecord record : download_records) {
                if (record.parent_dir.equals(header.parent_dir) && record.file_name.equals(header.file_name) && record.file_size == header.file_size) {
                    recorded = true;

                    break;
                }
            }
        }

        if (!recorded) {
            return false;
        }

        return diskMatches(header);
    }

    // 磁盘校验：目标文件存在且大小与头部声明一致
    static boolean diskMatches(File download_path, SyncBasic.FileHeader header) {

        File target = buildSavePath(download_path, header.parent_dir, header.file_name);

        return target.isFile() && target.length() == header.file_size;
    }

    private boolean diskMatches(SyncBasic.FileHeader header) {

        return diskMatches(download_path, header);
    }

    // 取相对父目录的首段作为任务(入队目录)名：服务端 parent_dir 首段即入队目录名
    static String firstDirComponent(String parent_dir) {

        if (parent_dir == null) {
            return "";
        }

        int slash = parent_dir.indexOf('/');
        int backslash = parent_dir.indexOf('\\');

        int cut = slash < 0 ? backslash : (backslash < 0 ? slash : Math.min(slash, backslash));

        return cut < 0 ? parent_dir : parent_dir.substring(0, cut);
    }

    // 发送回复字节：true -> '1'(发送文件 / 会话结束确认) false -> '0'(跳过)
    private void sendReply(TcpConnection connection, boolean should_send) throws SyncException {

        connection.sendByte(should_send ? '1' : '0');
    }

    // 同一个身份（父目录 + 文件名 + 大小）只保留一条：
    private void addRecord(SyncBasic.FileHeader header) {

        synchronized (records_lock) {
            for (DownloadRecord record : download_records) {
                if (record.parent_dir.equals(header.parent_dir) && record.file_name.equals(header.file_name) && record.file_size == header.file_size) {
                    return;
                }
            }

            DownloadRecord record = new DownloadRecord();
            record.parent_dir = header.parent_dir;
            record.file_name = header.file_name;
            record.file_size = header.file_size;

            download_records.add(record);
        }
    }

    // 任务级完成通知：一个入队目录下的文件全部处理完（接收或跳过）时在工作线程回调
    // 上层需自行保证线程安全（Android 侧用 runOnUiThread 回到主线程） 建议在首次下载前设置
    public void setTaskCallback(TaskCallback callback) {

        synchronized (task_lock) {
            task_callback = callback;
        }
    }

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
            setError("[warning] task callback failed: " + error);
        }
    }

    // 加载下载记录（文件不存在视为空记录 加载失败不致命 只记录错误信息）
    private SyncError loadRecords() {

        synchronized (records_lock) {
            download_records.clear();

            if (!records_path.isFile()) {
                return SyncError.NONE;
            }

            byte[] raw;

            try {
                raw = Files.readAllBytes(records_path.toPath());
            } catch (IOException error) {
                return SyncException.errorOf(error);
            }

            try {
                JSONArray array = new JSONArray(new String(raw, StandardCharsets.UTF_8));

                for (int i = 0; i < array.length(); i++) {
                    JSONObject item = array.getJSONObject(i);

                    DownloadRecord record = new DownloadRecord();
                    record.parent_dir = item.optString("parent_dir", "");
                    record.file_name = item.optString("file_name", "");
                    record.file_size = item.optLong("file_size", 0L);

                    download_records.add(record);
                }

                return SyncError.NONE;
            } catch (JSONException error) {
                return SyncError.INVALID_ARGUMENT;
            }
        }
    }

    // 保存下载记录（落盘方式沿用工程约定：写 .tmp 再替换 避免半截 JSON）
    private SyncError saveRecords() {

        synchronized (records_lock) {
            try {
                JSONArray array = new JSONArray();

                for (DownloadRecord record : download_records) {
                    JSONObject item = new JSONObject();
                    item.put("parent_dir", record.parent_dir);
                    item.put("file_name", record.file_name);
                    item.put("file_size", record.file_size);

                    array.put(item);
                }

                if (!download_path.isDirectory() && !download_path.mkdirs()) {
                    return SyncError.PERMISSION_DENIED;
                }

                File temp = new File(download_path, "records.json.tmp");

                Files.write(temp.toPath(), array.toString(2).getBytes(StandardCharsets.UTF_8));

                moveReplacing(temp, records_path);

                return SyncError.NONE;
            } catch (IOException error) {
                return SyncException.errorOf(error);
            } catch (JSONException error) {
                return SyncError.INVALID_ARGUMENT;
            }
        }
    }

    // 安全拼接保存路径：只允许相对组件 丢弃 .. 与盘符 防止目录穿越
    // 与 core 的 buildSavePath 一致 额外把外置存储不允许的字符也换成 '_'
    static File buildSavePath(File download_path, String parent_dir, String file_name) {

        File target = download_path;

        String normalized = parent_dir == null ? "" : parent_dir.replace('\\', '/');

        for (String component : normalized.split("/")) {
            if (component.isEmpty() || ".".equals(component) || "..".equals(component)) {
                continue;
            }

            if (component.length() == 2 && component.charAt(1) == ':') {
                continue; // 盘符
            }

            target = new File(target, component);
        }

        return new File(target, safeFileName(file_name));
    }

    // 文件名不得包含路径分隔符（core 只处理 / 和 \ 这里连 Windows / FAT 的非法字符一起换）
    static String safeFileName(String file_name) {

        if (file_name == null) {
            return "";
        }

        StringBuilder builder = new StringBuilder(file_name.length());

        for (int i = 0; i < file_name.length(); i++) {
            char ch = file_name.charAt(i);

            if (ILLEGAL_NAME_CHARS.indexOf(ch) >= 0 || ch < 0x20) {
                builder.append('_');
            } else {
                builder.append(ch);
            }
        }

        return builder.toString();
    }

    private int serverCount() {

        synchronized (servers_lock) {
            return servers.size();
        }
    }

    private SyncBasic.ServerInfo serverAt(int index) {

        synchronized (servers_lock) {
            if (index < 0 || index >= servers.size()) {
                return null;
            }

            return servers.get(index);
        }
    }

    private void closeConnection() {

        TcpConnection current = connection;
        connection = null;

        if (current != null) {
            current.close();
        }
    }

    private void notifyDownload(DownloadCallback callback, boolean success, SyncError error) {

        if (callback == null) {
            return;
        }

        try {
            callback.onFinished(success, error);
        } catch (Throwable error_in_callback) {
            setError("[warning] download callback failed: " + error_in_callback);
        }
    }

    private void setError(String message) {

        error_string = message;
    }

    private static void moveReplacing(File source, File target) throws IOException {

        try {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException atomic_failed) {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
