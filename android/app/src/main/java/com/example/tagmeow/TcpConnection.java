package com.example.tagmeow;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

// TCP 消息连接：4 字节大端长度前缀成帧 单条连接不重连

// 协议（必须与 core 逐字节一致）：
// 1. 文件头 = 4 字节大端长度前缀 + JSON {"parent_dir","file_name","file_size"}
// 2. 文件数据 = 裸字节 严格 file_size 个 没有任何帧头
// 3. 应答字节 = 单个裸字节 '1'(发送) / '0'(跳过) 没有长度前缀

// 可打断的阻塞读：core 用原子标志 + 轮询式 asio::read 实现 这里用
// setSoTimeout(轮询粒度) + 读满循环实现 语义完全相同 ——
// 空闲（接收超时但没数据）继续等不算错误 interrupt() 之后最迟一个轮询周期内退出

// Android 适配差异（Java 的限制 语义不变）：
// Java 的 Socket 没有可移植的写超时（SO_SNDTIMEO 不在 StandardSocketOptions 里）
// core 的 30 秒发送超时在这里改由 close() 兜底 —— Socket.close() 是线程安全的
// 能从别的线程打断正在阻塞的 read/write 所以 stop()/disconnect() 仍然有界返回

public final class TcpConnection {

    private final Socket socket;
    private final InputStream input;
    private final OutputStream output;

    // interrupt() 置位 打断轮询接收（对应 core 的 interrupted_）
    private volatile boolean interrupted = false;

    private String error_string = "";

    // 接收进度
    public static final class TransferProgress {

        private long bytes_received = 0L;

        public long getBytesReceived() {
            return bytes_received;
        }
    }

    public TcpConnection(Socket socket) throws SyncException {

        if (socket == null) {
            throw new SyncException(SyncError.NOT_CONNECTED);
        }

        this.socket = socket;

        try {
            this.input = socket.getInputStream();
            this.output = socket.getOutputStream();
        } catch (IOException error) {
            throw new SyncException(SyncError.NOT_CONNECTED, "cannot open the socket streams", error);
        }
    }

    // 设置套接字收超时 毫秒 0 = 无限
    // 使文件传输级阻塞 I/O 有界（写超时见类注释）
    public SyncError setTimeouts(int send_timeout_ms, int recv_timeout_ms) {

        error_string = "";

        if (!socket.isConnected() || socket.isClosed()) {
            error_string = "[warning] the socket is not connected";

            return SyncError.NOT_CONNECTED;
        }

        try {
            socket.setSoTimeout(recv_timeout_ms <= 0 ? 0 : recv_timeout_ms);

            return SyncError.NONE;
        } catch (SocketException error) {
            error_string = "[warning] cannot set the socket timeout: " + error;

            return SyncError.NOT_CONNECTED;
        }
    }

    // 发送文件头 JSON 不含文件数据
    public void sendHeader(SyncBasic.FileHeader header) throws SyncException {

        JSONObject json = new JSONObject();

        try {
            json.put("parent_dir", header.parent_dir);
            json.put("file_name", header.file_name);
            json.put("file_size", header.file_size);
            json.put("last_in_dir", header.last_in_dir); // 任务(目录)最后一个文件标记 旧端忽略该字段
        } catch (JSONException error) {
            throw new SyncException(SyncError.INVALID_ARGUMENT, "cannot build the file header", error);
        }

        sendMessage(json.toString().getBytes(StandardCharsets.UTF_8));
    }

    // 发送「会话结束」控制帧（不携带文件字段的 JSON 标记）
    // 服务端正常收尾时发送 接收端以收到该帧作为会话正常结束的唯一依据
    public void sendSessionEnd() throws SyncException {

        JSONObject json = new JSONObject();

        try {
            json.put("session_end", true);
        } catch (JSONException error) {
            throw new SyncException(SyncError.INVALID_ARGUMENT, "cannot build the session-end frame", error);
        }

        sendMessage(json.toString().getBytes(StandardCharsets.UTF_8));
    }

    // 接收文件头 严格校验必需字段（与 core 的 receiveHeader 一致）
    // 会话结束控制帧不校验文件字段 直接返回一个只带 session_end 标记的头部
    // 对端断开时抛 SyncException(EOF) —— 注意这**不再**代表会话正常结束
    public SyncBasic.FileHeader receiveHeader() throws SyncException {

        byte[] data = receiveMessage();

        String text = new String(data, StandardCharsets.UTF_8);

        try {
            JSONObject json = new JSONObject(text);

            // 会话结束控制帧：不携带文件字段 仅表示服务端正常收尾
            if (json.optBoolean("session_end", false)) {
                SyncBasic.FileHeader end_header = new SyncBasic.FileHeader();
                end_header.session_end = true;

                return end_header;
            }

            if (!json.has("parent_dir") || !json.has("file_name") || !json.has("file_size")) {
                throw new SyncException(SyncError.INVALID_ARGUMENT, "file header is missing fields");
            }

            SyncBasic.FileHeader header = new SyncBasic.FileHeader();
            header.parent_dir = json.getString("parent_dir");
            header.file_name = json.getString("file_name");
            header.file_size = json.getLong("file_size");
            header.last_in_dir = json.optBoolean("last_in_dir", false); // 可选字段 缺省 false

            return header;
        } catch (JSONException error) {
            throw new SyncException(SyncError.INVALID_ARGUMENT, "cannot parse the file header", error);
        }
    }

    // 发送一个文件数据块：从 offset 开始读最多 chunk_size 字节并发送
    // 返回实际发送的字节数 0 表示已到文件末尾（调用方据此停止 避免对端死等剩余字节）
    public int sendFileData(File file, long offset, int chunk_size) throws SyncException {

        if (file == null || !file.isFile()) {
            throw new SyncException(SyncError.NO_SUCH_FILE_OR_DIRECTORY, String.valueOf(file));
        }

        byte[] buffer;

        RandomAccessFile reader = null;

        try {
            reader = new RandomAccessFile(file, "r");

            if (offset < 0 || offset > reader.length()) {
                throw new SyncException(SyncError.INVALID_ARGUMENT, "bad offset " + offset);
            }

            reader.seek(offset);

            buffer = new byte[chunk_size <= 0 ? SyncBasic.FILE_CHUNK_SIZE : chunk_size];

            int read = reader.read(buffer);

            if (read <= 0) {
                return 0; // 正常 EOF：不发送任何字节
            }

            writeAll(buffer, 0, read);

            return read;
        } catch (IOException error) {
            throw new SyncException(SyncException.errorOf(error), error.getMessage(), error);
        } finally {
            closeQuietly(reader);
        }
    }

    // 接收文件数据到指定路径 内部循环接收直到 file_size 字节
    public void receiveFileTo(File save_path, long file_size, TransferProgress progress) throws SyncException {

        if (save_path == null) {
            throw new SyncException(SyncError.INVALID_ARGUMENT, "empty save path");
        }

        if (progress != null) {
            progress.bytes_received = 0L;
        }

        File parent = save_path.getParentFile();

        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new SyncException(SyncError.PERMISSION_DENIED, "cannot create " + parent.getAbsolutePath());
        }

        File part_path = new File(parent, save_path.getName() + ".part");

        byte[] buffer = new byte[SyncBasic.FILE_CHUNK_SIZE];

        long received = 0;
        boolean committed = false;

        OutputStream file = null;

        try {
            file = new FileOutputStream(part_path);

            while (received < file_size) {
                int want = (int) Math.min(buffer.length, file_size - received);

                readFully(buffer, 0, want);

                file.write(buffer, 0, want);

                received += want;

                if (progress != null) {
                    progress.bytes_received = received;
                }
            }

            // 落盘校验：flush/close 失败（磁盘满/网络盘写失败）不能当成功
            // 否则下载记录与文件实际内容不一致
            file.flush();
            file.close();
            file = null;

            // 字节数完整性校验
            if (part_path.length() != file_size) {
                throw new SyncException(SyncError.IO_ERROR, "short write " + part_path.length() + "/" + file_size);
            }

            if (progress != null) {
                progress.bytes_received = received;
            }

            moveReplacing(part_path, save_path);
            committed = true;
        } catch (SyncException error) {
            throw error;
        } catch (IOException error) {
            throw new SyncException(SyncException.errorOf(error), error.getMessage(), error);
        } finally {
            closeQuietly(file);

            if (!committed) {
                // 不留残留：目标路径上原有的文件始终没被碰过
                deleteQuietly(part_path);
            }
        }
    }

    // 发送单个控制字节 无长度前缀 用于客户端 1/0 回复协议
    public void sendByte(char value) throws SyncException {

        writeAll(new byte[]{(byte) value}, 0, 1);
    }

    // 接收单个控制字节 最多等待 timeout_ms
    // 返回读到的字节（0..255） 返回 -1 表示超时无数据
    // core 里这条路径是 `receiveByte 返回 false 且 ec = errc::timed_out` 调用方据此 continue
    // 对端关闭抛 SyncException(EOF)
    public int receiveByte(int timeout_ms) throws SyncException {

        int old_timeout;

        try {
            old_timeout = socket.getSoTimeout();
            socket.setSoTimeout(timeout_ms <= 0 ? 1 : timeout_ms);
        } catch (SocketException error) {
            throw new SyncException(SyncError.NOT_CONNECTED, error.getMessage(), error);
        }

        try {
            return input.read();
        } catch (SocketTimeoutException timeout) {
            return -1;
        } catch (IOException error) {
            throw new SyncException(SyncException.errorOf(error), error.getMessage(), error);
        } finally {
            // 读完之后恢复 避免影响后续文件数据传输（与 core 的临时 setsockopt 一致）
            try {
                socket.setSoTimeout(old_timeout);
            } catch (SocketException ignored) {
                // 套接字已经关了 恢复失败无所谓
            }
        }
    }

    // 打断阻塞中的接收（原子标志 线程安全）
    // 正在等待数据的接收最迟在一个接收轮询周期内返回 OPERATION_CANCELED
    public void interrupt() {
        interrupted = true;
    }

    public boolean isInterrupted() {
        return interrupted;
    }

    public void clearInterrupt() {
        interrupted = false;
    }

    // 关闭连接 线程安全：别的线程调用它会立刻打断阻塞中的 read/write
    public void close() {

        try {
            if (!socket.isClosed()) {
                socket.close();
            }
        } catch (IOException error) {
            error_string = "[warning] cannot close the connection: " + error;
        }
    }

    public Socket socket() {
        return socket;
    }

    public String getLastError() {
        return error_string;
    }

    // 发送一帧：4 字节大端长度前缀 + 数据
    private void sendMessage(byte[] data) throws SyncException {

        if (data.length > SyncBasic.MAX_FRAME_SIZE) {
            throw new SyncException(SyncError.MESSAGE_SIZE, "frame is too large: " + data.length);
        }

        byte[] frame = new byte[4 + data.length];

        frame[0] = (byte) ((data.length >>> 24) & 0xFF);
        frame[1] = (byte) ((data.length >>> 16) & 0xFF);
        frame[2] = (byte) ((data.length >>> 8) & 0xFF);
        frame[3] = (byte) (data.length & 0xFF);

        System.arraycopy(data, 0, frame, 4, data.length);

        writeAll(frame, 0, frame.length);
    }

    // 接收一帧 对端正常断开抛 SyncException(EOF)
    private byte[] receiveMessage() throws SyncException {

        byte[] prefix = new byte[4];

        readFully(prefix, 0, 4);

        // 长度前缀是大端 与 core 的 htonl/ntohl 一致
        long length = ((prefix[0] & 0xFFL) << 24) | ((prefix[1] & 0xFFL) << 16) | ((prefix[2] & 0xFFL) << 8) | (prefix[3] & 0xFFL);

        if (length > SyncBasic.MAX_FRAME_SIZE) {
            // 防御恶意/损坏的长度前缀导致超大内存分配
            throw new SyncException(SyncError.MESSAGE_SIZE, "frame length " + length);
        }

        byte[] data = new byte[(int) length];

        if (length > 0) {
            readFully(data, 0, (int) length);
        }

        return data;
    }

    // 轮询式完整读取：可被 interrupt() 打断
    // 空闲超时（接收超时但没数据）继续等待 不报错（对应 core 的 readInterruptible）
    private void readFully(byte[] data, int offset, int length) throws SyncException {

        int done = 0;

        while (done < length) {
            if (interrupted) {
                throw new SyncException(SyncError.OPERATION_CANCELED);
            }

            int read;

            try {
                read = input.read(data, offset + done, length - done);
            } catch (SocketTimeoutException idle) {
                continue; // 空闲：继续等
            } catch (IOException error) {
                throw new SyncException(SyncException.errorOf(error), error.getMessage(), error);
            }

            if (read < 0) {
                throw new SyncException(SyncError.EOF);
            }

            done += read;
        }
    }

    private void writeAll(byte[] data, int offset, int length) throws SyncException {

        if (interrupted) {
            throw new SyncException(SyncError.OPERATION_CANCELED);
        }

        try {
            output.write(data, offset, length);
            output.flush();
        } catch (IOException error) {
            throw new SyncException(SyncException.errorOf(error), error.getMessage(), error);
        }
    }

    // 改名覆盖到目标路径
    // 部分文件系统（外置存储上的 FAT/exFAT）不允许直接覆盖已存在目标：
    // 那就先删目标再改名一次
    private static void moveReplacing(File source, File target) throws IOException {

        try {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);

            return;
        } catch (IOException atomic_failed) {
            // 走下面的非原子路径
        }

        try {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException replace_failed) {
            if (target.exists() && !target.delete()) {
                throw replace_failed;
            }

            Files.move(source.toPath(), target.toPath());
        }
    }

    private static void deleteQuietly(File file) {

        if (file != null && file.exists() && !file.delete()) {
            // 删不掉也没办法 但至少不要盖掉真正的失败原因
        }
    }

    private static void closeQuietly(java.io.Closeable target) {

        if (target == null) {
            return;
        }

        try {
            target.close();
        } catch (IOException error) {
            // 关闭失败不改判上层结果
        }
    }
}
