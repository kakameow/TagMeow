package com.example.tagmeow;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

// 语言管理：按 id 取文案

// 职责（只有这三件）：
// 1. 收集语言列表：扫描语言目录下的 *.json
// 2. 加载 json：从语言目录读某个语言，或直接从 JSON 文本加载
// 3. 加载外部 json 到内部：外部导入的语言复制进语言目录 之后只扫这一个目录

// - 文件格式：{"name":"zh_CN","version":"1.0","text":[{"id":"bar.settings","str":"设置"}]}
// - 缺键 / 未知 id 返回 MISSING_STRING
// - 本类自己保证同步
public final class LanguageManager {

    // 缺键时的返回值 与原版一致
    public static final String MISSING_STRING = "MISSING_STRING";

    private static final String JSON_SUFFIX = ".json";

    private static final String KEY_TEXT = "text";

    private static final String KEY_ID = "id";

    private static final String KEY_STR = "str";

    private static final String KEY_NAME = "name";

    private String language_name = "";

    private final List<String> language_list = new ArrayList<>();

    private File language_directory;

    private final Map<String, String> dictionary = new LinkedHashMap<>();

    private String error_string = "";

    public LanguageManager(File language_directory) {
        loadLanguageList(language_directory);
    }

    // 1. 收集语言列表：语言名就是文件名（去掉 .json）
    public synchronized boolean loadLanguageList(File directory_path) {
        Objects.requireNonNull(directory_path);

        if (!directory_path.isDirectory()) {
            error_string = "[warning] Directory does not exist: " + directory_path;
            return false;
        }

        language_list.clear();
        language_directory = directory_path;

        File[] files = directory_path.listFiles();

        if (files != null) {
            for (File file : files) {
                if (!file.isFile() || !file.getName().endsWith(JSON_SUFFIX)) {
                    continue;
                }

                String name = file.getName().substring(0, file.getName().length() - JSON_SUFFIX.length());

                if (!name.isEmpty()) {
                    language_list.add(name);
                }
            }
        }

        Collections.sort(language_list);

        error_string = "";
        return true;
    }

    // 2. 加载 json：从语言目录里读某个语言
    public synchronized boolean loadLanguage(String language_name_utf8) {
        Objects.requireNonNull(language_name_utf8);

        if (language_directory == null) {
            error_string = "[warning] language directory is not set";
            return false;
        }

        File file = new File(language_directory, language_name_utf8 + JSON_SUFFIX);

        if (!file.isFile()) {
            error_string = "[warning] Cannot open language file: " + file;
            return false;
        }

        String text;

        try {
            text = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            error_string = "[warning] Cannot read language file: " + file;
            return false;
        }

        return loadFromJson(language_name_utf8, text);
    }

    // 2. 加载 json：直接从 JSON 文本加载
    public synchronized boolean loadFromJson(String language_name_utf8, String json_text) {
        Objects.requireNonNull(json_text);

        JSONObject config;

        try {
            config = new JSONObject(json_text);
        } catch (JSONException error) {
            error_string = "[warning] JSON parse error: " + error.getMessage();
            return false;
        }

        JSONArray items = config.optJSONArray(KEY_TEXT);

        if (items == null) {
            error_string = "[warning] Missing 'text' array in language file";
            return false;
        }

        dictionary.clear();

        String name = config.optString(KEY_NAME, "");
        language_name = name.isEmpty() ? language_name_utf8 : name;

        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.optJSONObject(i);

            if (item == null) {
                continue;
            }

            String id = item.optString(KEY_ID, "");

            if (id.isEmpty()) {
                continue;
            }

            dictionary.put(id, item.optString(KEY_STR, ""));
        }

        error_string = "";
        return true;
    }

    // 3. 加载外部 json 到内部：复制进语言目录并登记
    // 语言名默认取源文件名（去掉 .json），也可以显式指定
    // 坏的 json 不会被搬进内部目录
    public synchronized boolean importLanguage(File source_file, String language_name_utf8) {
        Objects.requireNonNull(source_file);

        if (language_directory == null) {
            error_string = "[warning] language directory is not set";
            return false;
        }

        if (!source_file.isFile()) {
            error_string = "[warning] Cannot open language file: " + source_file;
            return false;
        }

        String name = (language_name_utf8 == null || language_name_utf8.isEmpty())
                ? stripJsonSuffix(source_file.getName())
                : language_name_utf8;

        if (name.isEmpty()) {
            error_string = "[warning] Cannot tell the language name from: " + source_file;
            return false;
        }

        String text;

        try {
            text = new String(Files.readAllBytes(source_file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            error_string = "[warning] Cannot read language file: " + source_file;
            return false;
        }

        // 先当成语言文件校验一遍
        try {
            JSONObject config = new JSONObject(text);

            if (config.optJSONArray(KEY_TEXT) == null) {
                error_string = "[warning] Missing 'text' array in language file";
                return false;
            }
        } catch (JSONException error) {
            error_string = "[warning] JSON parse error: " + error.getMessage();
            return false;
        }

        if (!language_directory.isDirectory() && !language_directory.mkdirs()) {
            error_string = "[warning] Cannot create language directory: " + language_directory;
            return false;
        }

        File target = new File(language_directory, name + JSON_SUFFIX);

        try {
            Files.write(target.toPath(), text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException error) {
            error_string = "[warning] Cannot write language file: " + target;
            return false;
        }

        if (!language_list.contains(name)) {
            language_list.add(name);
            Collections.sort(language_list);
        }

        error_string = "";
        return true;
    }

    // 取文案：缺键 / 未知返回 MISSING_STRING
    public synchronized String getString(String id_utf8) {
        if (id_utf8 == null) {
            return MISSING_STRING;
        }

        String value = dictionary.get(id_utf8);

        return value == null ? MISSING_STRING : value;
    }

    public synchronized String getLanguageName() {
        return language_name;
    }

    public synchronized List<String> getLanguagesList() {
        return new ArrayList<>(language_list);
    }

    public synchronized String getLastError() {
        return error_string;
    }

    private static String stripJsonSuffix(String file_name) {
        if (file_name == null || !file_name.endsWith(JSON_SUFFIX)) {
            return file_name == null ? "" : file_name;
        }

        return file_name.substring(0, file_name.length() - JSON_SUFFIX.length());
    }
}
