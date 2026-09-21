package com.example.tagmeow;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.Comparator;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// 目录打包：把一棵目录树递归压成一个 zip

// 与同步功能一个路子：只依赖 java.io（不碰任何 Android 平台类），所以 JVM 单元测试里能直接跑

// 约定：
// 1. 条目名是相对路径 + '/'（zip 规范）；空目录补一条目录条目，解压后目录结构不丢
// 2. 先写 <名字>.part 再改名到最终文件（工程里统一用这套「写临时再替换」的落盘方式）
//    中途失败不会留下一个「看起来完整」的压缩包
// 3. 进度回调按时间节流（不是每个缓冲区都回一次）：大文件也有进度，又不会把 UI 线程淹掉
public final class DirectoryZipper {

    // 缓冲区大小：与同步传输的分块一致（64 KiB）
    private static final int BUFFER_SIZE = 64 * 1024;

    // 进度回调的最小间隔（毫秒）
    private static final long REPORT_INTERVAL_MS = 100L;

    // 进度回调（在工作线程上执行，不要在里面碰 UI）
    public interface Progress {
        void onProgress(String entry_name, int file_count, long byte_count);
    }

    // 打包结果
    public static final class Result {

        public final File zip_file;
        // 压缩进去的文件数
        public final int file_count;
        // 原始字节数（不是压缩后的体积）
        public final long byte_count;
        // 补进去的空目录条目数
        public final int empty_dir_count;

        Result(File zip_file, int file_count, long byte_count, int empty_dir_count) {
            this.zip_file = zip_file;
            this.file_count = file_count;
            this.byte_count = byte_count;
            this.empty_dir_count = empty_dir_count;
        }
    }

    private DirectoryZipper() {
    }

    // 把 source_dir 打包到 zip_file
    // 失败抛 IOException：目标目录建不出来、目录读不了（没权限时 listFiles 返回 null）
    public static Result zip(File source_dir, File zip_file, Progress progress) throws IOException {

        if (source_dir == null || !source_dir.isDirectory()) {
            throw new IOException("not a directory: " + source_dir);
        }

        if (zip_file == null) {
            throw new IOException("zip target is null");
        }

        File parent = zip_file.getParentFile();

        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("cannot create directory: " + parent);
        }

        File temp = new File(parent == null ? zip_file : parent, zip_file.getName() + ".part");
        Counter counter = new Counter();

        try {
            try (ZipOutputStream output = new ZipOutputStream(
                    new BufferedOutputStream(new FileOutputStream(temp), BUFFER_SIZE))) {

                // 压缩级别用默认值（Deflater.DEFAULT_COMPRESSION）
                walk(source_dir, "", output, counter, progress);
            }

            moveReplacing(temp, zip_file);
        } catch (IOException error) {
            deleteQuietly(temp);

            throw error;
        } catch (RuntimeException error) {
            deleteQuietly(temp);

            throw error;
        }

        return new Result(zip_file, counter.file_count, counter.byte_count, counter.empty_dir_count);
    }

    // 递归收集：目录按名字排序，打包结果稳定（同一棵树每次跑出来的条目顺序一样）
    private static void walk(File dir, String prefix, ZipOutputStream output, Counter counter,
            Progress progress) throws IOException {

        File[] children = dir.listFiles();

        if (children == null) {
            // 没权限时 listFiles() 只返回 null 什么都不说（与 StorageAccess 的处理一致）
            throw new IOException("cannot list directory (permission denied?): " + dir.getAbsolutePath());
        }

        if (children.length == 0) {
            // 空目录补一条目录条目（根目录本身不补：prefix 为空）
            if (!prefix.isEmpty()) {
                writeDirectoryEntry(dir, prefix, output);
                counter.empty_dir_count++;
            }

            return;
        }

        Arrays.sort(children, Comparator.comparing(File::getName));

        for (File child : children) {
            String relative = prefix + child.getName();

            if (child.isDirectory()) {
                walk(child, relative + "/", output, counter, progress);

                continue;
            }

            // 符号链接 / 设备文件之类的一律跳过：打包只负责普通文件
            if (!child.isFile()) {
                continue;
            }

            writeFileEntry(child, relative, output, counter, progress);
        }
    }

    private static void writeDirectoryEntry(File dir, String entry_name, ZipOutputStream output)
            throws IOException {

        ZipEntry entry = new ZipEntry(entry_name);
        entry.setTime(dir.lastModified());

        output.putNextEntry(entry);
        output.closeEntry();
    }

    private static void writeFileEntry(File file, String entry_name, ZipOutputStream output,
            Counter counter, Progress progress) throws IOException {

        ZipEntry entry = new ZipEntry(entry_name);
        entry.setTime(file.lastModified());
        entry.setSize(file.length());

        output.putNextEntry(entry);

        byte[] buffer = new byte[BUFFER_SIZE];

        try (InputStream input = new FileInputStream(file)) {
            int read = input.read(buffer);

            while (read > 0) {
                output.write(buffer, 0, read);

                counter.byte_count += read;
                report(progress, counter, entry_name, false);

                read = input.read(buffer);
            }
        }

        output.closeEntry();

        counter.file_count++;
        report(progress, counter, entry_name, true);
    }

    // 按时间节流：文件边界一定回一次，缓冲区边界最多每 REPORT_INTERVAL_MS 回一次
    private static void report(Progress progress, Counter counter, String entry_name, boolean force) {

        if (progress == null) {
            return;
        }

        long now = System.currentTimeMillis();

        if (!force && now - counter.last_report_ms < REPORT_INTERVAL_MS) {
            return;
        }

        counter.last_report_ms = now;
        progress.onProgress(entry_name, counter.file_count, counter.byte_count);
    }

    private static void moveReplacing(File source, File target) throws IOException {

        try {
            Files.move(
                    source.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException atomicFailed) {
            Files.move(
                    source.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void deleteQuietly(File file) {
        if (file != null && file.exists()) {
            file.delete();
        }
    }

    // 计数与节流状态（一次打包一份）
    private static final class Counter {

        int file_count;
        long byte_count;
        int empty_dir_count;
        long last_report_ms;
    }
}
