package com.example.tagmeow;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

// 全局标签定义库。
// 维护：
// - Type -> Tag
// - Type -> Color
// - Tag  -> Type
// 规则：
// - Tag 名称全局唯一
// - Type 名称全局唯一
// 本类不直接修改真实文件上的标签

// 配置文件格式与 Windows 原版保持一致：
// {
//     "groups": [
//         { "type": "work", "color": "#FFC0CB", "tags": ["a", "b"] }
//     ]
// }

public final class TagLibrary {

    // JSON
    public static final String DEFAULT_COLOR = "#FFC0CB";
    private static final String KEY_GROUPS = "groups";
    private static final String KEY_TYPE = "type";
    private static final String KEY_COLOR = "color";
    private static final String KEY_TAGS = "tags";

    // 配置文件
    private final File config_file;
    // type -> tags
    private final Map<String, List<String>> type_tags = new LinkedHashMap<>();
    // type -> color
    private final Map<String, String> type_colors = new LinkedHashMap<>();
    // 最后一次错误信息
    private String error_string = "";

    public TagLibrary(File config_file) {
        this.config_file = Objects.requireNonNull(config_file);

        if (!loadTagsFromFile(config_file)) {
            File parent = config_file.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }

            try {
                if (!config_file.exists()) {
                    Files.write(config_file.toPath(), new byte[0]);
                }
            } catch (IOException error) {
                // 保留原始错误信息
            }

            error_string = "[warning] File not found";
        }
    }

    // 从指定 JSON 文件加载标签库（整体替换）不会改变 saveTagsToFile() 的目标文件
    public boolean loadTagsFromFile(File file) {
        Objects.requireNonNull(file);

        if (!file.isFile()) {
            error_string = "[warning] Cannot open config file: " + file;
            return false;
        }

        String text;
        try {
            text = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            error_string = "[warning] Cannot open config file: " + file;
            return false;
        }

        return loadFromJson(text);
    }

    // 从 JSON 文本整体替换当前标签库
    public boolean loadFromJson(String json_text) {
        Objects.requireNonNull(json_text);

        Library parsed = parseJson(json_text);

        if (parsed == null) {
            return false;
        }

        type_tags.clear();
        type_colors.clear();
        type_tags.putAll(parsed.type_tags);
        type_colors.putAll(parsed.type_colors);

        clearInvalidTag();
        return true;
    }

    // 合并标签 如果本类的 tag_json 不存在或者为空 直接用 tag_json_file 内容替换数据
    // 一 比较 type 由于全局唯一 只添加本类没有的
    // 二 比较 tag 将本类没有的 tag 添加到 tag 对应的 type 下面
    // 三 将多余的数据抛弃 理论上说不会出现
    //
    // 导入标签库走的就是这个方法（不会整体覆盖已有内容）
    public boolean mergeTags(File tag_json_file) {
        Objects.requireNonNull(tag_json_file);

        if (!tag_json_file.isFile()) {
            error_string = "[warning] Cannot open config file: " + tag_json_file;
            return false;
        }

        String text;
        try {
            text = new String(Files.readAllBytes(tag_json_file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            error_string = "[warning] Cannot open config file: " + tag_json_file;
            return false;
        }

        return mergeFromJson(text);
    }

    // 从 JSON 文本合并进当前标签库
    public boolean mergeFromJson(String json_text) {
        Objects.requireNonNull(json_text);

        // 本类为空（文件不存在 / 空文件 / 没有 groups）时：直接整体替换
        if (type_colors.isEmpty() && type_tags.isEmpty()) {
            return loadFromJson(json_text);
        }

        Library incoming = parseJson(json_text);

        if (incoming == null) {
            return false;
        }

        mergeIncoming(incoming);
        error_string = "";
        return true;
    }

    // 一 只添加本类没有的 type（颜色一起带过来）
    // 二 已有的 type 保留本类颜色，只补它没有的 tag
    // 三 incoming 里多出来的数据自然被丢掉，本类自己的数据不动
    private void mergeIncoming(Library incoming) {
        for (Map.Entry<String, String> entry : incoming.type_colors.entrySet()) {
            String type = entry.getKey();
            List<String> source = incoming.type_tags.get(type);

            if (type_colors.containsKey(type)) {
                List<String> target = type_tags.computeIfAbsent(type, key -> new ArrayList<>());

                if (source != null) {
                    for (String tag : source) {
                        if (!hasTag(tag) && !target.contains(tag)) {
                            target.add(tag);
                        }
                    }
                }

                continue;
            }

            List<String> target = new ArrayList<>();

            if (source != null) {
                for (String tag : source) {
                    if (!hasTag(tag) && !target.contains(tag)) {
                        target.add(tag);
                    }
                }
            }

            type_colors.put(type, entry.getValue());
            type_tags.put(type, target);
        }

        clearInvalidTag();
    }

    // 解析 JSON 文本到临时结构（不动本类数据）
    // 语法错误返回 null；没有 groups 时返回空结构并留下 "[tip] JSON is empty"
    private Library parseJson(String json_text) {
        JSONObject config;

        try {
            config = new JSONObject(json_text);
        } catch (JSONException error) {
            error_string = "[warning] File format error: " + error.getMessage();
            return null;
        }

        Library parsed = new Library();

        if (!config.has(KEY_GROUPS) || !(config.opt(KEY_GROUPS) instanceof JSONArray)) {
            error_string = "[tip] JSON is empty";
            return parsed;
        }

        JSONArray groups = config.optJSONArray(KEY_GROUPS);

        for (int i = 0; groups != null && i < groups.length(); i++) {
            JSONObject group = groups.optJSONObject(i);

            if (group == null) {
                continue;
            }

            String type = group.optString(KEY_TYPE, "");

            if (type.isEmpty()) {
                continue;
            }

            String color = group.optString(KEY_COLOR, DEFAULT_COLOR);

            if (!parsed.type_colors.containsKey(type) && isValidHexColor(color)) {
                parsed.type_colors.put(type, color);
            } else if (!isValidHexColor(color)) {
                parsed.type_colors.put(type, DEFAULT_COLOR);
            }

            // 无论该类型是否有标签 都在 type_tags 登记（空类型也保留）
            List<String> tags = parsed.type_tags.computeIfAbsent(type, key -> new ArrayList<>());

            JSONArray tag_array = group.optJSONArray(KEY_TAGS);

            for (int j = 0; tag_array != null && j < tag_array.length(); j++) {
                String tag = tag_array.optString(j, "");

                if (!tag.isEmpty()) {
                    tags.add(tag);
                }
            }
        }

        dedupe(parsed.type_tags);
        error_string = "";
        return parsed;
    }

    // 去掉空 tag 并保证 tag 全局唯一
    private static void dedupe(Map<String, List<String>> tags) {
        Set<String> seen = new LinkedHashSet<>();

        for (Map.Entry<String, List<String>> entry : tags.entrySet()) {
            List<String> filtered = new ArrayList<>(entry.getValue().size());

            for (String tag : entry.getValue()) {
                if (tag == null || tag.isEmpty()) {
                    continue;
                }

                if (seen.add(tag)) {
                    filtered.add(tag);
                }
            }

            entry.setValue(filtered);
        }
    }

    // 解析结果：导入 / 合并时先解析到临时结构，再决定整体替换还是并进来
    private static final class Library {

        final Map<String, List<String>> type_tags = new LinkedHashMap<>();

        final Map<String, String> type_colors = new LinkedHashMap<>();
    }

    // 保存到当前配置文件
    public boolean saveTagsToFile() {
        if (config_file == null || config_file.getPath().isEmpty()) {
            error_string = "[warning] No config file path has been set";
            return false;
        }

        JSONObject root = new JSONObject();
        JSONArray groups = new JSONArray();

        // 以 type_colors 的键为准遍历 保证空类型也会被保存
        for (String type : getAllTypeNames()) {
            JSONObject group = new JSONObject();

            try {
                group.put(KEY_TYPE, type);
                group.put(KEY_COLOR, type_colors.getOrDefault(type, ""));
                group.put(KEY_TAGS, new JSONArray(type_tags.getOrDefault(type, new ArrayList<>())));
            } catch (JSONException error) {
                error_string = "[warning] Failed to write JSON: " + error.getMessage();
                return false;
            }

            groups.put(group);
        }

        try {
            root.put(KEY_GROUPS, groups);
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
            data = root.toString(4).getBytes(StandardCharsets.UTF_8);
        } catch (JSONException error) {
            error_string = "[warning] Failed to write JSON: " + error.getMessage();
            return false;
        }

        try {
            Files.write(temp.toPath(), data);
            moveReplacing(temp, config_file);
        } catch (IOException error) {
            error_string = "[warning] Failed to rename temp file: " + error.getMessage();
            if (temp.exists()) {
                temp.delete();
            }
            return false;
        }

        error_string = "";
        return true;
    }

    // 检查颜色格式
    public static boolean isValidHexColor(String color) {
        if (color == null || color.isEmpty()) {
            return false;
        }

        int start = color.charAt(0) == '#' ? 1 : 0;
        int length = color.length() - start;

        if (length != 3 && length != 4 && length != 6 && length != 8) {
            return false;
        }

        for (int i = start; i < color.length(); i++) {
            char c = color.charAt(i);

            boolean hex = (c >= '0' && c <= '9')
                    || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F');

            if (!hex) {
                return false;
            }
        }

        return true;
    }

    // 添加一个 Tag; Tag 名称必须全局唯一
    public boolean addTag(
            String tag,
            String type) {

        if (tag == null || tag.isEmpty()) {
            error_string = "[warning] tag name cannot be empty";
            return false;
        }

        if (hasTag(tag)) {
            error_string = "";
            return true;
        }

        if (!hasType(type)) {
            error_string = "[warning] type is not found ";
            return false;
        }

        type_tags.computeIfAbsent(type, key -> new ArrayList<>()).add(tag);
        error_string = "";
        return true;
    }

    // 删除一个 Tag
    public boolean removeTag(String tag) {
        for (List<String> tags : type_tags.values()) {
            if (tags.remove(tag)) {
                error_string = "";
                return true;
            }
        }

        error_string = "[warning] tag is not found ";
        return false;
    }

    // 重命名一个 Tag
    public boolean renameTag(String old_tag, String new_tag) {

        if (old_tag == null || new_tag == null || old_tag.isEmpty() || new_tag.isEmpty()) {
            error_string = "[warning] Tag name cannot be empty";
            return false;
        }

        if (old_tag.equals(new_tag)) {
            return true;
        }

        if (hasTag(new_tag)) {
            error_string = "[warning] New tag already exists: " + new_tag;
            return false;
        }

        boolean found = false;
        for (List<String> tags : type_tags.values()) {
            int index = tags.indexOf(old_tag);
            if (index >= 0) {
                tags.set(index, new_tag);
                found = true;
                break;
            }
        }

        if (!found) {
            error_string = "[warning] Tag not found: " + old_tag;
            return false;
        }

        error_string = "";
        return true;
    }

    // 设置 Tag 所属 Type
    public boolean setTagType(String tag, String new_type) {

        if (!hasTag(tag)) {
            error_string = "Tag not found: " + tag;
            return false;
        }

        if (!hasType(new_type)) {
            error_string = "Type not found: " + new_type;
            return false;
        }

        for (List<String> tags : type_tags.values()) {
            if (tags.remove(tag)) {
                break;
            }
        }

        type_tags.computeIfAbsent(new_type, key -> new ArrayList<>()).add(tag);
        error_string = "";
        return true;
    }

    // 判断 Tag 是否存在
    public boolean hasTag(String tag) {
        if (tag == null) {
            return false;
        }

        for (List<String> tags : type_tags.values()) {
            if (tags.contains(tag)) {
                error_string = "";
                return true;
            }
        }

        return false;
    }

    // 获取 Tag 所属 Type 不存在返回空字符串
    public String getTypeOfTag(String tag) {
        if (tag != null) {
            for (Map.Entry<String, List<String>> entry : type_tags.entrySet()) {
                if (entry.getValue().contains(tag)) {
                    return entry.getKey();
                }
            }
        }

        error_string = "[warning] tag is not found";
        return "";
    }

    // 获取所有 Tag 名称
    public List<String> getAllTagNames() {
        List<String> result = new ArrayList<>();

        for (List<String> tags : type_tags.values()) {
            result.addAll(tags);
        }

        return result;
    }

    // 根据前缀自动补全 前缀为空时返回空列表
    public List<String> autoComplete(String prefix) {
        List<String> result = new ArrayList<>();

        if (prefix == null || prefix.isEmpty()) {
            return result;
        }

        for (List<String> tags : type_tags.values()) {
            for (String tag : tags) {
                if (tag.startsWith(prefix)) {
                    result.add(tag);
                }
            }
        }

        return result;
    }

    // 清理非法标签 去掉空标签 并且保证 Tag 全局唯一
    public boolean clearInvalidTag() {
        Set<String> seen = new LinkedHashSet<>();
        boolean changed = false;

        for (Map.Entry<String, List<String>> entry : type_tags.entrySet()) {
            List<String> filtered = new ArrayList<>(entry.getValue().size());

            for (String tag : entry.getValue()) {
                if (tag == null || tag.isEmpty()) {
                    changed = true;
                    continue;
                }

                if (seen.add(tag)) {
                    filtered.add(tag);
                } else {
                    changed = true;
                }
            }

            entry.setValue(filtered);
        }

        return changed;
    }

    // 添加一个 Type 使用默认颜色
    public boolean addType(String type) {
        return addType(type, DEFAULT_COLOR);
    }

    // 添加一个 Type 并指定颜色 颜色非法时退回默认颜色
    public boolean addType(String type, String color) {

        if (type == null || type.isEmpty()) {
            error_string = "[warning] type name cannot be empty";
            return false;
        }

        if (hasType(type)) {
            error_string = "";
            return true;
        }

        // 颜色非法时退回默认颜色（与原版一致：成功后清空错误状态）
        String final_color = isValidHexColor(color) ? color : DEFAULT_COLOR;

        type_colors.put(type, final_color);
        // 登记空类型：0 标签的类型同样存在
        type_tags.computeIfAbsent(type, key -> new ArrayList<>());

        error_string = "";
        return true;
    }

    // 删除一个 Type
    public boolean removeType(String type) {
        if (!type_colors.containsKey(type)) {
            error_string = "[tip] type not found";
            return false;
        }

        type_colors.remove(type);
        type_tags.remove(type);

        error_string = "";
        return true;
    }

    // 重命名一个 Type
    public boolean renameType(String old_type, String new_type) {

        if (old_type == null || new_type == null || old_type.isEmpty() || new_type.isEmpty()) {
            error_string = "[warning] Type name cannot be empty";
            return false;
        }

        if (old_type.equals(new_type)) {
            return true;
        }

        if (hasType(new_type)) {
            error_string = "[warning] New type already exists: " + new_type;
            return false;
        }

        if (!type_tags.containsKey(old_type)) {
            error_string = "[warning] Type not found: " + old_type;
            return false;
        }

        // 保持原有顺序：用新的键替换旧键
        Map<String, List<String>> reordered_tags = new LinkedHashMap<>();
        Map<String, String> reordered_colors = new LinkedHashMap<>();

        for (Map.Entry<String, List<String>> entry : type_tags.entrySet()) {
            if (entry.getKey().equals(old_type)) {
                reordered_tags.put(new_type, entry.getValue());
            } else {
                reordered_tags.put(entry.getKey(), entry.getValue());
            }
        }

        for (Map.Entry<String, String> entry : type_colors.entrySet()) {
            if (entry.getKey().equals(old_type)) {
                reordered_colors.put(new_type, entry.getValue());
            } else {
                reordered_colors.put(entry.getKey(), entry.getValue());
            }
        }

        type_tags.clear();
        type_tags.putAll(reordered_tags);

        type_colors.clear();
        type_colors.putAll(reordered_colors);

        error_string = "";
        return true;
    }

    // 设置 Type 颜色
    public boolean setTypeColor(String type, String new_color) {

        if (!hasType(type)) {
            error_string = "[warning] Type not found: " + type;
            return false;
        }

        if (!isValidHexColor(new_color)) {
            error_string = "[warning] Invalid color format: " + new_color;
            return false;
        }

        type_colors.put(type, new_color);
        error_string = "";
        return true;
    }

    // 判断 Type 是否存在
    public boolean hasType(String type) {
        return type != null && type_colors.containsKey(type);
    }

    // 获取 Type 对应颜色 不存在返回空字符串
    public String getColorByType(String type) {
        String color = type_colors.get(type);
        if (color != null) {
            return color;
        }

        error_string = "[warning] type is not found";
        return "";
    }

    // 获取所有 Type 名称
    public List<String> getAllTypeNames() {
        return new ArrayList<>(type_colors.keySet());
    }

    // 获取 Type -> Tag 的深拷贝 只读
    public Map<String, List<String>> getTypeTag() {
        Map<String, List<String>> copy = new LinkedHashMap<>();

        for (Map.Entry<String, List<String>> entry : type_tags.entrySet()) {
            copy.put(
                    entry.getKey(),
                    Collections.unmodifiableList(new ArrayList<>(entry.getValue())));
        }

        return Collections.unmodifiableMap(copy);
    }

    // 获取 Type -> Color 的拷贝
    public Map<String, String> getTypeColor() {
        return Collections.unmodifiableMap(new LinkedHashMap<>(type_colors));
    }

    // 返回当前配置文件
    public File getConfigFile() {
        return config_file;
    }

    // 返回最后一次错误
    public String getLastError() {
        return error_string;
    }

    private static void moveReplacing(File source, File target) throws IOException {

        try {
            Files.move(
                    source.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException atomic_failed) {
            Files.move(
                    source.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING);
        }
    }
}