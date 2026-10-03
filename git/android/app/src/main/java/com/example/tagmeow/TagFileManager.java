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
    // 读取宽松：名字里任意位置出现的 {[..]} 都算一个标签作用域 一个名字里可以有多个 全部读出来
    // 但只有「{ + [ + 内容 + ] + }」这种正确双重包裹的才算一个标签作用域
    // 单层包裹的 {} / [] / {] / ]] / {df} 之类一律不算标签
    private static final Pattern TAG_BLOCK_PATTERN = Pattern.compile("\\{\\[([^\\]\\}]*)\\]\\}");
    // 写入 / 删除时用的匹配：只认「名字结尾」那一个（紧挨扩展名也算结尾）
    // 严格以原文件名为准：添加时加在名字尾 去掉时也只去掉结尾那一个
    // 一样必须是 {[..]} 双重包裹
    // 单层的 {df} 不是标签块 必须原样保留 不能被正则吃掉（10{df}.mp4 不能变成 10.mp4）
    private static final Pattern TAIL_TAG_BLOCK_PATTERN = Pattern.compile("\\{\\[[^\\]\\}]*\\]\\}(?=(\\.[^.]*)?$)");
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

        // 最后一个标签被删掉：要真的把标签从存储里去掉
        // （Filename 模式就是把名字里的标签块去掉 空写入是不动文件名的）
        if (tags.isEmpty()) {
            if (removeModeTags(file, default_mode)) {
                error_string = "";
                return true;
            }

            // 带上具体原因 不然界面只看到「删除失败」不知道为什么
            error_string = "[warning] removal failed: " + error_string;
            return false;
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

        // 删空了跟单标签删除一样：走 removeModeTags 真的清掉存储
        if (new_tags.isEmpty()) {
            if (removeModeTags(file, default_mode)) {
                error_string = "";
                return true;
            }

            error_string = "[warning] removal failed: " + error_string;
            return false;
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

        if (to_mode == StoreMode.FILENAME) {
            // 目标模式是 Filename：把名字里已经有的标签并进来
            for (String tag : parseFromFilename(file.getName())) {
                if (!tags.contains(tag)) {
                    tags.add(tag);
                }
            }
        }

        if (!writeTagsToFile(file, tags, to_mode)) {
            error_string = "[warning] write failed";
            return false;
        }

        // 写入可能改了文件名 删旧格式要用改名之后的路径
        FileRef current = last_written_path == null ? file : last_written_path;

        if (!keep_old && !removeModeTags(current, from_mode)) {
            error_string = "[warning] remove old tags failed: " + error_string;
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
            // 整个名字就是一个 {[..]} 作用域（例如 {[test]}.mp4）：去掉作用域之后连名字都没了
            // 先在名字前面补一个毫秒时间戳当名字部分（原名称原样跟在后面）再照常去作用域
            // 只有真要改名字时才补 纯读取 / 索引一个字节都不动
            FileRef target = file;
            String materialized = materializeWholeNameBlock(file.getName());

            if (!materialized.equals(file.getName())) {
                target = file.getParent().child(materialized);
            }

            FileRef clean_path = removeFilenameTagsPath(target);

            if (!clean_path.equals(file)
                    && !storage.rename(file, clean_path.getName())) {
                error_string = "[warning] Failed to rename file when removing filename tags";
                return false;
            }

            // 名字变了 侧车跟着搬
            if (!clean_path.equals(file)) {
                moveSidecar(file, clean_path);
            }

            last_written_path = clean_path;
        } else {
            FileRef sidecar_path = buildSidecarPath(file);

            if (!removeSidecar(sidecar_path)) {
                error_string = "[warning] Failed to remove sidecar file: " + sidecar_path;
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
            // 侧车严格按「真实文件名」定位 和写入端完全一致
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

    // 整个名字（扩展名之前）就是一个 {[..]} 作用域 例如 {[test]}.mp4
    // 这种名字「去掉结尾作用域」之后什么都不剩 改名就等于把原文件名删掉
    public static boolean isWholeNameTagBlock(String file_name) {
        if (file_name == null || file_name.isEmpty()) {
            return false;
        }

        int index = extensionIndex(file_name);
        String stem = index < 0 ? file_name : file_name.substring(0, index);

        return !stem.isEmpty() && removeTagsFromFilename(stem).isEmpty();
    }

    // 整名作用域的文件没有能当"名字"的部分：{[test]}.mp4 去掉作用域就什么都不剩
    // 这种在真的要改名字的时候补一个毫秒时间戳前缀： <毫秒时间戳> + 原名称
    // 原名称一个字符都不丢 只是前面多了一段能当名字的东西
    // 前缀只能加在最前面：作用域必须留在名字结尾 加在后面以后就再也去不掉了
    // 纯读取 / 索引 / 同步扫描都不会走到这里 文件不会因为"被读了一下"就改名
    public static String materializeWholeNameBlock(String file_name) {
        return materializeWholeNameBlock(file_name, System.currentTimeMillis());
    }

    // 时间戳由调用方给（测试用 免得断言依赖真实时间）
    static String materializeWholeNameBlock(String file_name, long millis) {
        if (!isWholeNameTagBlock(file_name)) {
            return file_name;
        }

        return millis + file_name;
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
    // 读取宽松：名字里所有 {[..]} 作用域都读 位置不限（名字中间 / 扩展名后面都算）
    // 只有 {[..]} 双重包裹的才算作用域 单层的 {} [] {] ]] 之类不读
    // 同一个标签写在多个作用域里只算一个
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
                // 多个作用域里重复出现的标签只留第一个
                if (!tag.isEmpty() && !tags.contains(tag)) {
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

    // 去掉结尾的标签块 返回纯文件名（名字中间的部分原样保留）
    // 只有 {[..]} 双重包裹才算标签块：{df} / {} / {] 这种单层的原样留着 一个字符都不动
    // 循环去：file{[a]}{[b]}.txt 这种连着写好几个的要全部去掉
    public static String removeTagsFromFilename(String file_name) {
        if (file_name == null || file_name.isEmpty()) {
            return file_name;
        }

        String current = file_name;
        String next = TAIL_TAG_BLOCK_PATTERN.matcher(current).replaceAll("");

        while (!next.equals(current)) {
            current = next;
            next = TAIL_TAG_BLOCK_PATTERN.matcher(current).replaceAll("");
        }

        return current;
    }

    private boolean writeTagsToFile(FileRef file, List<String> tags, StoreMode mode) {

        if (!storage.exists(file)) {
            return false;
        }

        if (storage.isDirectory(file) || mode == StoreMode.SIDECAR) {
            // 跟读取端一致：按真实文件名放侧车
            FileRef sidecar_path = buildSidecarPath(file);

            if (tags == null || tags.isEmpty()) {
                if (!removeSidecar(sidecar_path)) {
                    return false;
                }

                last_written_path = file;
                return true;
            }

            if (!writeSidecar(sidecar_path, tags)) {
                return false;
            }

            // 只写了侧车 文件本身没动
            last_written_path = file;
            return true;
        }

        // 标签为空时不动文件名
        // 转换 / 写入时"源里没标签"是正常情况 不能因此把名字里原有的标签块抹掉
        if (tags == null || tags.isEmpty()) {
            last_written_path = file;
            return true;
        }

        // 整名作用域：本来就没有名字部分 先补一个毫秒时间戳前缀再写标签
        // （前缀只能加在最前面 作用域必须留在名字结尾 加在后面以后就再也去不掉了）
        String name = materializeWholeNameBlock(file.getName());
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

        // 文件名变了：侧车跟着改名
        FileRef renamed = file.getParent().child(new_name);
        moveSidecar(file, renamed);

        // 文件名变了 记下新路径供上层做增量索引更新
        last_written_path = renamed;
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

    // 文件名改了：把侧车一起改名
    private boolean moveSidecar(FileRef from, FileRef to) {
        FileRef old_sidecar = buildSidecarPath(from);

        if (!storage.exists(old_sidecar)) {
            return true;
        }

        return storage.rename(old_sidecar, buildSidecarPath(to).getName());
    }

    // 删掉侧车文件 并在 .tag 目录变空时把该目录一并删掉
    private boolean removeSidecar(FileRef sidecar_path) {
        if (storage.exists(sidecar_path) && !storage.delete(sidecar_path)) {
            return false;
        }

        FileRef sidecar_dir = sidecar_path.getParent();

        if (sidecar_dir == null || !storage.exists(sidecar_dir)) {
            return true;
        }

        try {
            if (storage.listChildren(sidecar_dir).isEmpty()) {
                storage.delete(sidecar_dir);
            }
        } catch (IOException error) {
            // 列不出来就不动目录 侧车本身已经删掉了
        }

        return true;
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