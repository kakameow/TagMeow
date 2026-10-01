package com.example.tagmeow;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

// 日志：把各个类的错误信息追加到磁盘（对应 Windows 端的 Log 类）
//
// 约定（与 Windows 端保持一致）：
// 1. 一行一条：[YYYY-MM-DD HH:MM:SS] 类名: 消息
// 2. 每写一行就 flush：这是拿来排查问题的 不能等缓冲区
// 3. 单个文件超过 1MB 时 下次打开会先清空
// 4. 同一个类的同一条消息不重复写（上层每次操作后会把所有类的 lastError 收一遍）
//
// 日志文件放在默认下载目录的同级（<外部存储>/TagMeow/log.txt）
// 没有「所有文件访问」权限时由调用方退回应用自己的目录
public final class AppLog {

    // 单个日志文件的上限：到点了就在下次打开时清空
    private static final long MAX_BYTES = 1024 * 1024;

    private static final Object lock = new Object();

    // 每个类上次写过的消息：一样就跳过
    private static final Map<String, String> last_logged = new LinkedHashMap<>();

    private static File log_file;
    private static BufferedWriter writer;
    private static String error_string = "";

    private AppLog() {
    }

    // 打开日志（追加写）文件超过上限时先清空
    public static void init(File file) {
        synchronized (lock) {
            closeNoLock();

            if (file == null) {
                error_string = "[warning] log file is null";
                return;
            }

            File parent = file.getParentFile();

            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                error_string = "[warning] Cannot create log directory: " + parent;
                return;
            }

            if (file.isFile() && file.length() >= MAX_BYTES) {
                // 涨到上限：这一轮从空文件重新开始（对应 Windows 端构造里的 clearLog）
                last_logged.clear();
            }

            boolean append = file.isFile() && file.length() < MAX_BYTES;

            try {
                writer = new BufferedWriter(new OutputStreamWriter(
                        new FileOutputStream(file, append), StandardCharsets.UTF_8));
            } catch (IOException error) {
                writer = null;
                error_string = "[warning] Cannot open log file: " + error.getMessage();
                return;
            }

            log_file = file;
            error_string = "";
        }
    }

    // 写一条：空消息跳过 同一个类的同一条消息只写一次
    public static void write(String class_name, String message) {
        if (message == null || message.isEmpty()) {
            return;
        }

        synchronized (lock) {
            if (writer == null) {
                return;
            }

            String name = (class_name == null || class_name.isEmpty()) ? "App" : class_name;

            if (message.equals(last_logged.get(name))) {
                return;
            }

            last_logged.put(name, message);

            try {
                writer.write(timeStamp() + " " + name + ": " + message);
                writer.newLine();
                writer.flush();
                error_string = "";
            } catch (IOException error) {
                error_string = "[warning] Cannot write log file: " + error.getMessage();
            }
        }
    }

    // 清空日志（截断重开）：清完同一条消息可以再写一次
    public static void clear() {
        synchronized (lock) {
            last_logged.clear();
            closeNoLock();

            if (log_file == null) {
                return;
            }

            try {
                writer = new BufferedWriter(new OutputStreamWriter(
                        new FileOutputStream(log_file, false), StandardCharsets.UTF_8));
                error_string = "";
            } catch (IOException error) {
                writer = null;
                error_string = "[warning] Cannot clear log file: " + error.getMessage();
            }
        }
    }

    public static void close() {
        synchronized (lock) {
            closeNoLock();
        }
    }

    // 日志文件路径（还没打开时是空串）
    public static String path() {
        synchronized (lock) {
            return log_file == null ? "" : log_file.getAbsolutePath();
        }
    }

    public static String getLastError() {
        synchronized (lock) {
            return error_string;
        }
    }

    // 一行的时间前缀 [YYYY-MM-DD HH:MM:SS]
    private static String timeStamp() {
        return "[" + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date()) + "]";
    }

    private static void closeNoLock() {
        if (writer == null) {
            return;
        }

        try {
            writer.flush();
            writer.close();
        } catch (IOException error) {
            error_string = "[warning] Cannot close log file: " + error.getMessage();
        }

        writer = null;
    }
}
