package com.example.tagmeow;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// 单个文件的标签存储格式处理器

// 负责：
// - 读取 / 写入文件标签
// - 添加 / 删除标签
// - Filename ↔ Sidecar 转换
// - 删除指定模式下的标签数据

// 不负责：
// - 管理目录配置
// - 管理全局 TagLibrary
// - SQLite 查询
// - 整体目录扫描

// 本类不保存 roots 列表
// 目录信息由 FileRef.root_id 携带

// Filename 模式格式示例：
// 文件{[标签1,标签2]}.txt

// Sidecar 模式格式示例（与被管理文件同级目录的 .tag 目录下）：
// dir/.tag/file.txt.json
// JSON 内容：{"tags":"标签1,标签2"}

// 文件可以使用 Filename / Sidecar
// 文件夹只能使用 Sidecar（重命名目录会破坏内部路径关系）

public final class TagFileManager {

    // Sidecar 目录名称
    public static final String TAG_DIRECTORY = ".tag";
    // 匹配文件名中的标签块 {[标签1,标签2]}
    private static final Pattern TAG_BLOCK_PATTERN = Pattern.compile("\\{\\[([^\\]]+)\\]\\}");
    // 匹配文件名中任意 {xxx} 块 用于清理
    private static final Pattern BRACE_BLOCK_PATTERN = Pattern.compile("\\{[^\\}]*\\}");
    // Sidecar 文件中标签字段的键名
    private static final String SIDECAR_KEY = "tags";
    // 封装类
    private final StorageAccess storage;
    // 默认模式
    private StoreMode default_mode;
    // 最后一次成功写入后文件实际所在的路径 Filename 模式改过名字时这里记录的就是新路径
    private FileRef last_written_path;
    // 最后一次操作的错误信息 为空表示没有错误
    private String error_string = "";

    public TagFileManager(StorageAccess storage, StoreMode default_mode) {

        this.storage = Objects.requireNonNull(storage);
        this.default_mode = default_mode == null ? StoreMode.SIDECAR : default_mode;
    }

    public void setDefaultMode(StoreMode mode) {
        if (mode != null) {
            this.default_mode = mode;
        }
    }

    public StoreMode getDefaultMode() {
        return default_mode;
    }

    // 最后一次文件标签操作实际写入的路径 没有发生过写入时返回 null
    // 只修改文件名的时候（Filename 模式）调用方仍需知道文件现在叫什么
    public FileRef getLastWrittenPath() {
        return last_written_path;
    }

    public String getLastError() {
        return error_string;
    }

    // 使用默认模式添加标签
    public boolean addTag(FileRef file, String tag) {

        Objects.requireNonNull(file);

        if (tag == null || tag.isEmpty()) {
            error_string = "";
            return true;
        }

        List<String> tags = extractTags(file, default_mode);
        if (tags.contains(tag)) {
            error_string = "";
            return true;
        }

        tags.add(tag);

        if (writeTagsToFile(file, tags, default_mode)) {
            error_string = "";
            return true;
        }

        error_string = "[warning] addition failed";
        return false;
    }

    // 使用默认模式删除单个标签
    public boolean removeTag(FileRef file, String tag) {

        Objects.requireNonNull(file);

        if (tag == null || tag.isEmpty()) {
            error_string = "";
            return true;
        }

        List<String> tags = extractTags(file, default_mode);
        if (!tags.remove(tag)) {
            error_string = "";
            return true;
        }

        if (writeTagsToFile(file, tags, default_mode)) {
            error_string = "";
            return true;
        }

        error_string = "[warning] removal failed";
        return false;
    }

    // 使用默认模式批量删除标签 与 Windows 原版一致：结果按字典序排列
    public boolean removeTag(FileRef file, List<String> tags) {

        Objects.requireNonNull(file);

        if (tags == null || tags.isEmpty()) {
            error_string = "";
            return true;
        }

        List<String> current_tags = new ArrayList<>(extractTags(file, default_mode));
        Collections.sort(current_tags);

        List<String> sorted_remove = new ArrayList<>(tags);
        Collections.sort(sorted_remove);

        List<String> new_tags = new ArrayList<>(current_tags.size());
        for (String tag : current_tags) {
            if (!sorted_remove.contains(tag)) {
                new_tags.add(tag);
            }
        }

        if (new_tags.equals(current_tags)) {
            error_string = "";
            return true;
        }

        if (writeTagsToFile(file, new_tags, default_mode)) {
            error_string = "";
            return true;
        }

        error_string = "[warning] removal failed";
        return false;
    }

    // 单个文件模式转换原语
    // from == to 且 keep_old 为 true 时不做任何事
    // from == to 且 keep_old 为 false 时表示删除该模式的标签
    public boolean convertMode(FileRef file, StoreMode from_mode, StoreMode to_mode, boolean keep_old) {

        Objects.requireNonNull(file);
        Objects.requireNonNull(from_mode);
        Objects.requireNonNull(to_mode);

        if (from_mode == to_mode && keep_old) {
            error_string = "";
            return true;
        }

        List<String> tags = extractTags(file, from_mode);
        if (!writeTagsToFile(file, tags, to_mode)) {
            error_string = "[warning] write failed";
            return false;
        }

        if (!keep_old && !removeModeTags(file, from_mode)) {
            error_string = "[warning] remove old tags failed";
            return false;
        }

        error_string = "";
        return true;
    }

    // 删除文件在指定模式下存储的标签
    // Filename：重命名去掉标签块
    // Sidecar：删除侧车文件
    public boolean removeModeTags(FileRef file, StoreMode mode) {

        Objects.requireNonNull(file);
        Objects.requireNonNull(mode);

        if (mode == StoreMode.FILENAME) {
            FileRef clean_path = removeFilenameTagsPath(file);

            if (!clean_path.equals(file)
                    && !storage.rename(file, clean_path.getName())) {
                error_string = "[warning] Failed to rename file when removing filename tags";
                return false;
            }

            last_written_path = clean_path;
        } else {
            FileRef sidecar_path = buildCleanSidecarPath(file);

            if (storage.exists(sidecar_path) && !storage.delete(sidecar_path)) {
                error_string = "[warning] Failed to remove sidecar file";
                return false;
            }

            // 只删了侧车 文件本身没动
            last_written_path = file;
        }

        error_string = "";
        return true;
    }

    // 读取指定模式的标签
    // 目录永远按 Sidecar 处理
    public List<String> extractTags(FileRef file, StoreMode mode) {

        Objects.requireNonNull(file);

        if (storage.isDirectory(file) || mode == StoreMode.SIDECAR) {
            List<String> tags = readSidecar(buildSidecarPath(file));
            return tags == null ? new ArrayList<>() : tags;
        }

        return parseFromFilename(file.getName());
    }

    // 不指定模式读取标签
    // 默认模式优先 读不到标签时再尝试另一种模式
    public List<String> extractTags(FileRef file) {
        Objects.requireNonNull(file);

        List<String> tags = extractTags(file, default_mode);
        if (!tags.isEmpty()) {
            return tags;
        }

        StoreMode fallback = default_mode == StoreMode.SIDECAR
                ? StoreMode.FILENAME
                : StoreMode.SIDECAR;

        return extractTags(file, fallback);
    }

    // 构建侧车文件路径
    // <parent>/.tag/<文件名>.json
    public static FileRef buildSidecarPath(FileRef file) {
        Objects.requireNonNull(file);

        String name = file.getName();
        String parent = file.getParentPath();

        String relative = parent.isEmpty()
                ? TAG_DIRECTORY + "/" + name + ".json"
                : parent + "/" + TAG_DIRECTORY + "/" + name + ".json";

        return new FileRef(file.getRootId(), relative);
    }

    // 构建"无标签"侧车文件路径 先去除文件名中的标签块再定位侧车
    public static FileRef buildCleanSidecarPath(FileRef file) {
        return buildSidecarPath(removeFilenameTagsPath(file));
    }

    // 从文件路径中去除文件名里的标签块
    public static FileRef removeFilenameTagsPath(FileRef file) {
        Objects.requireNonNull(file);

        String name = file.getName();
        if (name.isEmpty()) {
            return file;
        }

        int index = extensionIndex(name);
        String stem = index < 0 ? name : name.substring(0, index);
        String extension = index < 0 ? "" : name.substring(index);
        String clean_name = removeTagsFromFilename(stem) + extension;

        if (clean_name.equals(name)) {
            return file;
        }

        String parent = file.getParentPath();
        String relative = parent.isEmpty() ? clean_name : parent + "/" + clean_name;

        return new FileRef(file.getRootId(), relative);
    }

    // 从文件名中解析出标签列表
    public static List<String> parseFromFilename(String file_name) {
        List<String> tags = new ArrayList<>();

        if (file_name == null || file_name.isEmpty()) {
            return tags;
        }

        Matcher matcher = TAG_BLOCK_PATTERN.matcher(file_name);
        while (matcher.find()) {
            String block = matcher.group(1);
            if (block == null) {
                continue;
            }

            for (String tag : block.split(",")) {
                if (!tag.isEmpty()) {
                    tags.add(tag);
                }
            }
        }

        return tags;
    }

    // 将基础文件名和标签列表组合成带标签的新文件名 传入的 file_name 不带扩展名
    public static String formatFilenameWithTags(String file_name, List<String> tags) {

        if (file_name == null || tags == null || tags.isEmpty()) {
            return file_name;
        }

        return file_name + "{[" + join(tags) + "]}";
    }

    // 从文件名中移除所有标签块 返回纯文件名
    public static String removeTagsFromFilename(String file_name) {
        if (file_name == null || file_name.isEmpty()) {
            return file_name;
        }

        return BRACE_BLOCK_PATTERN.matcher(file_name).replaceAll("");
    }

    private boolean writeTagsToFile(FileRef file, List<String> tags, StoreMode mode) {

        if (!storage.exists(file)) {
            return false;
        }

        if (storage.isDirectory(file) || mode == StoreMode.SIDECAR) {
            if (!writeSidecar(buildCleanSidecarPath(file), tags)) {
                return false;
            }

            // 只写了侧车 文件本身没动
            last_written_path = file;
            return true;
        }

        String name = file.getName();
        int index = extensionIndex(name);
        String stem = index < 0 ? name : name.substring(0, index);
        String extension = index < 0 ? "" : name.substring(index);

        String new_name = formatFilenameWithTags(removeTagsFromFilename(stem), tags) + extension;

        if (new_name.equals(name)) {
            last_written_path = file;
            return true;
        }

        if (!storage.rename(file, new_name)) {
            return false;
        }

        // 文件名变了 记下新路径供上层做增量索引更新
        last_written_path = file.getParent().child(new_name);
        return true;
    }

    // 读取侧车文件 读取失败返回 null 读取成功但无标签返回空列表
    private List<String> readSidecar(FileRef sidecar_path) {
        if (!storage.exists(sidecar_path)) {
            return null;
        }

        byte[] raw;
        try {
            raw = storage.readAll(sidecar_path);
        } catch (IOException error) {
            return null;
        }

        if (raw == null || raw.length == 0) {
            return null;
        }

        JSONObject json;
        try {
            json = new JSONObject(new String(raw, StandardCharsets.UTF_8));
        } catch (JSONException error) {
            return null;
        }

        if (!json.has(SIDECAR_KEY)) {
            return null;
        }

        List<String> tags = new ArrayList<>();
        Object value = json.opt(SIDECAR_KEY);

        if (value instanceof String) {
            String text = (String) value;
            if (text.isEmpty()) {
                return tags;
            }

            for (String tag : text.split(",")) {
                if (!tag.isEmpty()) {
                    tags.add(tag);
                }
            }

            return tags;
        }

        if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            for (int i = 0; i < array.length(); i++) {
                String tag = array.optString(i, "");
                if (!tag.isEmpty()) {
                    tags.add(tag);
                }
            }

            return tags;
        }

        return null;
    }

    // 写入侧车文件
    private boolean writeSidecar(FileRef sidecar_path, List<String> tags) {

        FileRef parent = sidecar_path.getParent();
        if (!storage.createDirectories(parent)) {
            return false;
        }

        JSONObject json = new JSONObject();

        byte[] data;
        try {
            json.put(SIDECAR_KEY, join(tags));
            data = json.toString(4).getBytes(StandardCharsets.UTF_8);
        } catch (JSONException error) {
            return false;
        }

        try {
            storage.writeAll(sidecar_path, data, true);
        } catch (IOException error) {
            return false;
        }

        return true;
    }

    // 以 ',' 连接标签
    private static String join(List<String> tags) {
        StringBuilder builder = new StringBuilder();

        for (int i = 0; i < tags.size(); i++) {
            if (i > 0) {
                builder.append(',');
            }

            builder.append(tags.get(i));
        }

        return builder.toString();
    }

    // 扩展名起始下标 没有扩展名时返回 -1
    private static int extensionIndex(String file_name) {
        if (file_name.isEmpty() || file_name.equals(".") || file_name.equals("..")) {
            return -1;
        }

        int dot = file_name.lastIndexOf('.');
        if (dot <= 0) {
            // 前导点开头的隐藏文件视为没有扩展名
            return -1;
        }

        return dot;
    }
}