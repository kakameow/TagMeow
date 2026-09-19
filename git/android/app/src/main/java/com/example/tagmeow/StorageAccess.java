package com.example.tagmeow;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

// 存储层：直接用绝对路径访问文件系统

// 用途：
// 1. 「所有文件访问」存储模式（应用里唯一的存储方式）：
//    拿到 MANAGE_EXTERNAL_STORAGE 之后 UI 用绝对路径直接管理目录
//    完全不走系统选择器（有些 ROM 的选择器会拒绝授权任何目录）
// 2. JVM 单元测试：不依赖任何 Android 平台类 在 JVM 里直接就能跑

// 以前这里是 StorageAccess 接口 + SAF/本地两个实现
// SAF 那套跟着系统选择器一起砍了（见 git 03191d2） 接口也就合并进来了

// root_id 与绝对路径一一对应 所以不依赖任何 Android 平台类

public final class StorageAccess {

    // root_id -> 根目录
    private final Map<String, File> roots = new HashMap<>();

    // 最后一次失败的原因（没有权限时 File.listFiles() 只返回 null 什么都不说）
    private String error_string = "";

    public StorageAccess() {
    }

    // 使用已经存在的目录初始化一个 root
    public StorageAccess(File rootDirectory) {
        Objects.requireNonNull(rootDirectory);

        String root_id = normalizePath(rootDirectory.getAbsolutePath());
        addRoot(root_id, root_id);
    }

    public synchronized void addRoot(String root_id, String locator) {

        Objects.requireNonNull(root_id);
        Objects.requireNonNull(locator);

        roots.put(root_id, new File(normalizePath(locator)));
    }

    public synchronized void removeRoot(String root_id) {
        roots.remove(root_id);
    }

    // 最后一次失败的原因
    public synchronized String getLastError() {
        return error_string;
    }

    // 解析 FileRef 到实际文件
    // root 未注册时返回 null
    public synchronized File resolve(FileRef ref) {
        Objects.requireNonNull(ref);

        File root = roots.get(ref.getRootId());
        if (root == null) {
            return null;
        }

        String relative = ref.getRelativePath();
        if (relative.isEmpty()) {
            return root;
        }

        return new File(root, relative);
    }

    public synchronized boolean exists(FileRef ref) {
        File file = resolve(ref);
        return file != null && file.exists();
    }

    public synchronized boolean isDirectory(FileRef ref) {
        File file = resolve(ref);
        return file != null && file.isDirectory();
    }

    public synchronized List<FileRef> listChildren(FileRef directory) throws IOException {
        List<FileRef> result = new ArrayList<>();

        File dir = resolve(directory);
        if (dir == null) {
            error_string = "root is not registered: " + directory.getRootId();
            throw new IOException(error_string);
        }

        if (!dir.isDirectory()) {
            return result;
        }

        File[] children = dir.listFiles();
        if (children == null) {
            // 「所有文件访问」没开的时候这里就是 null
            // 以前返回空表：应用内浏览看起来只是「空目录」 一点错误提示都没有
            error_string = "cannot list directory (permission denied?): " + dir.getAbsolutePath();
            throw new IOException(error_string);
        }

        error_string = "";

        Arrays.sort(children, Comparator.comparing(File::getName));

        for (File child : children) {
            result.add(directory.child(child.getName()));
        }

        return result;
    }

    public synchronized byte[] readAll(FileRef ref) throws IOException {
        File file = resolve(ref);
        if (file == null) {
            throw new IOException("root is not registered: " + ref.getRootId());
        }

        return Files.readAllBytes(file.toPath());
    }

    public synchronized void writeAll(FileRef ref, byte[] data, boolean atomic) throws IOException {

        Objects.requireNonNull(ref);
        Objects.requireNonNull(data);

        File file = resolve(ref);
        if (file == null) {
            throw new IOException("root is not registered: " + ref.getRootId());
        }

        File parent = file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("cannot create parent directory: " + parent);
        }

        if (!atomic) {
            writeDirect(file, data);
            return;
        }

        File temp = new File(parent == null ? file : parent, file.getName() + ".tmp");

        try {
            writeDirect(temp, data);
            moveReplacing(temp, file);
        } catch (IOException error) {
            // 安全替换失败时退回直接写入 保证内容最终可用
            deleteQuietly(temp);
            writeDirect(file, data);
        }
    }

    public synchronized boolean createDirectories(FileRef directory) {
        File dir = resolve(directory);
        if (dir == null) {
            return false;
        }

        if (dir.isDirectory()) {
            return true;
        }

        return dir.mkdirs();
    }

    public synchronized boolean delete(FileRef ref) {
        File file = resolve(ref);
        if (file == null || !file.exists()) {
            return false;
        }

        return deleteRecursively(file);
    }

    public synchronized boolean rename(FileRef ref, String new_name) {

        Objects.requireNonNull(new_name);

        File file = resolve(ref);
        if (file == null || !file.exists()) {
            return false;
        }

        if (new_name.isEmpty() || new_name.indexOf('/') >= 0) {
            return false;
        }

        if (new_name.equals(file.getName())) {
            return true;
        }

        File target = new File(file.getParentFile(), new_name);

        try {
            moveReplacing(file, target);
            return true;
        } catch (IOException error) {
            return file.renameTo(target);
        }
    }

    public synchronized long lastModified(FileRef ref) {
        File file = resolve(ref);
        return file == null ? 0L : file.lastModified();
    }

    public synchronized long size(FileRef ref) {
        File file = resolve(ref);
        if (file == null || file.isDirectory()) {
            return 0L;
        }

        return file.length();
    }

    public synchronized String locatorOf(FileRef ref) {
        File file = resolve(ref);
        if (file == null || !file.exists()) {
            return null;
        }

        return normalizePath(file.getAbsolutePath());
    }

    public FileRef childOf(FileRef directory, String name) {

        return directory.child(name);
    }

    // 去掉末尾分隔符的路径
    public static String normalizePath(String path) {
        String unified = path.replace('\\', '/');

        while (unified.length() > 1 && unified.endsWith("/")) {
            unified = unified.substring(0, unified.length() - 1);
        }

        return unified;
    }

    private static void writeDirect(File file, byte[] data) throws IOException {

        try (OutputStream output = new FileOutputStream(file)) {
            output.write(data);
            output.flush();
        }
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

    private static boolean deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }

        return file.delete();
    }

    private static void deleteQuietly(File file) {
        if (file != null && file.exists()) {
            deleteRecursively(file);
        }
    }

    // 供调试使用
    public String toString() {
        return "StorageAccess" + Collections.unmodifiableMap(roots);
    }
}
