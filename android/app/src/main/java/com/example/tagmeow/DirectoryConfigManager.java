package com.example.tagmeow;

import android.net.Uri;
import android.provider.DocumentsContract;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

// 管理程序允许/管理的根目录
// 职责：
// 1. 保存用户选择的根目录
// 2. 加载/保存目录配置
// 3. 判断目录是否有效
// 4. 清理无效目录
// 不负责：
// - 遍历目录
// - 读取文件标签
// - 修改真实文件
// - SQLite
// - 线程安全

// 配置文件格式：
// {
//     "managed_dirs": [
//         {
//             "id": "primary:Documents",
//             "uri": "content://.../tree/primary%3ADocuments",
//             "display_name": "Documents"
//         }
//     ]
// }
// 同时兼容原版的纯字符串数组写法

class DirectoryConfigManager {

    public static final class Directory {

        // 根目录唯一标识
        private final String id;
        // Android SAF Tree Uri
        private final Uri uri;
        // 用于 UI 展示的目录名称
        private final String display_name;
        // 当前访问是否有效
        private boolean valid;

        public Directory(String id, Uri uri, String display_name, boolean valid) {
            this.id = Objects.requireNonNull(id);
            this.uri = Objects.requireNonNull(uri);
            this.display_name = display_name == null ? "" : display_name;
            this.valid = valid;
        }

        public String getId() {
            return id;
        }

        public Uri getUri() {
            return uri;
        }

        public String getDisplayName() {
            return display_name;
        }

        public boolean isValid() {
            return valid;
        }

        @Override
        public String toString() {
            return display_name + " [" + id + "]";
        }
    }

    // 判断一个目录当前是否可访问
    @FunctionalInterface
    public interface DirectoryValidator {
        boolean isValid(String root_id, Uri uri);
    }

    // JSON 配置文件中使用的键名
    private static final String KEY_MANAGED_DIRS = "managed_dirs";
    private static final String KEY_ID = "id";
    private static final String KEY_URI = "uri";
    private static final String KEY_DISPLAY_NAME = "display_name";

    // 配置文件路径
    private final File config_file;
    // 目录有效性校验器
    private final DirectoryValidator validator;
    // 当前加载的目录列表
    private final List<Directory> directories = new ArrayList<>();
    // 最近一次成功添加/解析的有效目录
    private Directory last_valid_directory;
    // 最近一次错误信息
    private String error_string = "";

    // 加载配置 不验证有效性
    public DirectoryConfigManager(File config_file) {
        this(config_file, null);
    }

    public DirectoryConfigManager(File config_file, DirectoryValidator validator) {

        this.config_file = Objects.requireNonNull(config_file);
        this.validator = validator;

        if (!loadFromFile()) {
            File parent = config_file.getParentFile();
            if (parent != null && !parent.exists()) {
                if (!parent.mkdirs()) {
                    error_string = "[warning] Cannot create config directory: " + parent;
                }
            }

            try {
                if (!config_file.exists()) {
                    Files.write(config_file.toPath(), new byte[0]);
                    error_string = "[warning] File not found";
                }
            } catch (IOException error) {
                error_string = "[warning] Failed to create config file: " + error.getMessage();
            }
        }
    }

    // 从配置文件加载目录
    public boolean loadFromFile() {
        if (!config_file.isFile()) {
            error_string = "[warning] Cannot open config file: " + config_file;
            return false;
        }

        String text;
        try {
            text = new String(Files.readAllBytes(config_file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            error_string = "[warning] Cannot open config file: " + config_file;
            return false;
        }

        JSONObject config;
        try {
            config = new JSONObject(text);
        } catch (JSONException error) {
            error_string = "[warning] File format error: " + error.getMessage();
            return false;
        }

        directories.clear();
        last_valid_directory = null;

        if (config.has(KEY_MANAGED_DIRS) && config.opt(KEY_MANAGED_DIRS) instanceof JSONArray) {
            JSONArray array = config.optJSONArray(KEY_MANAGED_DIRS);

            for (int i = 0; array != null && i < array.length(); i++) {
                Object element = array.opt(i);

                if (element instanceof String) {
                    addLoadedDirectory(null, (String) element, null);
                    continue;
                }

                JSONObject object = array.optJSONObject(i);
                if (object == null) {
                    continue;
                }

                String uri_text = object.optString(KEY_URI, "");
                if (uri_text.isEmpty()) {
                    continue;
                }

                addLoadedDirectory(object.optString(KEY_ID, ""), uri_text, object.optString(KEY_DISPLAY_NAME, ""));
            }

            error_string = "";
            return true;
        }

        error_string = "[tip] JSON is empty";
        return true;
    }

    // 保存当前目录配置
    public boolean saveToFile() {
        if (config_file.getPath().isEmpty()) {
            error_string = "[warning] No config file path has been set";
            return false;
        }

        JSONArray array = new JSONArray();

        for (Directory directory : directories) {
            JSONObject object = new JSONObject();

            try {
                object.put(KEY_ID, directory.getId());
                object.put(KEY_URI, directory.getUri().toString());
                object.put(KEY_DISPLAY_NAME, directory.getDisplayName());
            } catch (JSONException error) {
                error_string = "[warning] Failed to write JSON: " + error.getMessage();
                return false;
            }

            array.put(object);
        }

        JSONObject config = new JSONObject();
        try {
            config.put(KEY_MANAGED_DIRS, array);
        } catch (JSONException error) {
            error_string = "[warning] Failed to write JSON: " + error.getMessage();
            return false;
        }

        File parent = config_file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            error_string = "[warning] Cannot create config directory: " + parent;
            return false;
        }

        File temp = new File(parent, config_file.getName() + ".tmp");

        byte[] data;
        try {
            data = config.toString(4).getBytes(StandardCharsets.UTF_8);
        } catch (JSONException error) {
            error_string = "[warning] Failed to write JSON: " + error.getMessage();
            return false;
        }

        try {
            Files.write(temp.toPath(), data);
            moveReplacing(temp, config_file);
        } catch (IOException error) {
            error_string = "[warning] Failed to rename temporary file to config file: " + error.getMessage();
            if (temp.exists()) {
                if (temp.exists() && !temp.delete()) {
                    error_string += "[warning] (also failed to delete temp file)";
                }
            }
            return false;
        }

        error_string = "";
        return true;
    }

    // 添加一个受管理目录  目录不允许重复
    public boolean addDirectory(Uri tree_uri) {
        Objects.requireNonNull(tree_uri);

        String root_id = makeRootId(tree_uri);

        if (!isDirectoryValid(root_id, tree_uri)) {
            error_string = "[warning] directory does not exist or no permission";
            return false;
        }

        int existing = indexOf(root_id, tree_uri.toString());

        if (existing >= 0) {
            // 已经在管理列表里：把这次「添加」当成重新授权
            // 目录读不到时提示语就是让用户点「+ 添加」重新授权一次，
            // 以前这里直接返回 false：授权明明拿回来了也进不去，索引也不会重扫，
            // 用户就被锁在「读不到 -> 重新添加 -> 还是读不到」里出不来
            Directory again = new Directory(root_id, tree_uri, displayNameOf(tree_uri), true);

            directories.set(existing, again);
            last_valid_directory = again;

            error_string = "";
            return true;
        }

        Directory directory = new Directory(root_id, tree_uri, displayNameOf(tree_uri), true);

        directories.add(directory);
        last_valid_directory = directory;

        error_string = "";
        return true;
    }

    // 按 Uri 删除目录
    public boolean removeDirectory(Uri tree_uri) {
        Objects.requireNonNull(tree_uri);

        int index = indexOf(makeRootId(tree_uri), tree_uri.toString());
        if (index < 0) {
            error_string = "[tip] not found";
            return false;
        }

        Directory removed = directories.remove(index);
        if (removed == last_valid_directory) {
            last_valid_directory = null;
        }

        error_string = "";
        return true;
    }

    // 按 root_id 删除目录
    public boolean removeDirectory(String root_id) {
        if (root_id == null) {
            return false;
        }

        int index = indexOf(root_id, null);
        if (index < 0) {
            error_string = "[tip] not found";
            return false;
        }

        Directory removed = directories.remove(index);
        if (removed == last_valid_directory) {
            last_valid_directory = null;
        }

        error_string = "";
        return true;
    }

    // 清除已经失效 无法访问的目录
    public void clearInvalidPath() {
        List<Directory> valid = new ArrayList<>(directories.size());

        for (Directory directory : directories) {
            directory.valid = isDirectoryValid(directory.getId(), directory.getUri());

            if (directory.valid) {
                valid.add(directory);
            }
        }

        directories.clear();
        directories.addAll(valid);

        if (last_valid_directory != null && !last_valid_directory.isValid()) {
            last_valid_directory = null;
        }

        error_string = "";
    }

    // 返回当前所有目录
    public List<Directory> getDirList() {
        return List.copyOf(directories);
    }

    // 返回当前所有有效目录
    public List<Directory> getValidDirList() {
        List<Directory> result = new ArrayList<>(directories.size());

        for (Directory directory : directories) {
            if (directory.isValid()) {
                result.add(directory);
            }
        }

        return result;
    }

    // 返回最近一次成功添加/解析的有效目录 没有有效目录时返回 null
    public Directory getLastValidDirectory() {
        if (last_valid_directory != null && last_valid_directory.isValid()) {
            return last_valid_directory;
        }

        for (int i = directories.size() - 1; i >= 0; i--) {
            Directory directory = directories.get(i);
            if (directory.isValid()) {
                return directory;
            }
        }

        return null;
    }

    // 根据 root_id 查找目录 不存在返回 null
    public Directory getDirectory(String root_id) {
        int index = indexOf(root_id, null);
        return index < 0 ? null : directories.get(index);
    }

    // 判断某个 root_id 是否属于受管理目录
    public boolean containsRoot(String root_id) {
        return indexOf(root_id, null) >= 0;
    }

    // 返回配置文件
    public File getConfigFile() {
        return config_file;
    }

    // 返回最后一次错误
    public String getLastError() {
        return error_string;
    }

    // 由 Uri 推导稳定的 root_id; SAF tree Uri 使用 tree document id; file Uri 使用绝对路径
    public static String makeRootId(Uri uri) {
        Objects.requireNonNull(uri);

        if ("content".equalsIgnoreCase(uri.getScheme())) {
            try {
                String document_id = DocumentsContract.getTreeDocumentId(uri);
                if (document_id != null && !document_id.isEmpty()) {
                    return document_id;
                }
            } catch (IllegalArgumentException error) {
                // 不是 tree Uri 忽略
            }
        }

        if ("file".equalsIgnoreCase(uri.getScheme())) {
            String path = uri.getPath();
            if (path != null && !path.isEmpty()) {
                return path;
            }
        }

        return uri.toString();
    }

    // 由 Uri 推导用于 UI 展示的目录名称
    public static String displayNameOf(Uri uri) {
        String id = makeRootId(uri);

        int slash = id.lastIndexOf('/');
        int colon = id.lastIndexOf(':');
        int cut = Math.max(slash, colon);

        String name = (cut >= 0 && cut < id.length() - 1) ? id.substring(cut + 1) : id;

        try {
            return Uri.decode(name);
        } catch (RuntimeException error) {
            return name;
        }
    }

    // 添加一个已经加载的目录
    private void addLoadedDirectory(String id, String uri_text, String display_name) {

        Uri uri;
        try {
            uri = Uri.parse(uri_text);
        } catch (RuntimeException error) {
            return;
        }

        String root_id = (id == null || id.isEmpty()) ? makeRootId(uri) : id;

        if (indexOf(root_id, uri.toString()) >= 0) {
            return;
        }

        String name = (display_name == null || display_name.isEmpty()) ? displayNameOf(uri) : display_name;

        Directory directory = new Directory(root_id, uri, name, isDirectoryValid(root_id, uri));

        directories.add(directory);

        if (directory.isValid()) {
            last_valid_directory = directory;
        }
    }

    // 判断目录是否有效
    private boolean isDirectoryValid(String root_id, Uri uri) {

        if (validator == null) {
            // 没有注入校验器时无法判断 视为有效
            return true;
        }

        return validator.isValid(root_id, uri);
    }

    // 根据 root_id 或 Uri 查找目录索引
    private int indexOf(String root_id, String uri_text) {

        for (int i = 0; i < directories.size(); i++) {
            Directory directory = directories.get(i);

            if (root_id != null && root_id.equals(directory.getId())) {
                return i;
            }

            if (uri_text != null && uri_text.equals(directory.getUri().toString())) {
                return i;
            }
        }

        return -1;
    }

    // 将 source 文件移动到 target 文件位置 若 target 已存在则覆盖
    private static void moveReplacing(File source, File target) throws IOException {

        try {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException atomicFailed) {
            Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }
}