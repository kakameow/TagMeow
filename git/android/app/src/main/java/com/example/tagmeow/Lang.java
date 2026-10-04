package com.example.tagmeow;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

// 语言门面：UI 只调用 Lang.get("key") / Lang.f("key", args)

// 语言文件只放一个位置：Android/data/<包名>/files/language/*.json（应用的外部私有目录 用户能直接改）
// - 内置语言（assets/language/*.json）：目录为空（或少了某个语言文件）时整个铺进去
// - 已经在的语言文件只补「内置里有、它没有」的键 用户改过的文案不会被应用更新冲掉
// - 外部导入的语言（Lang.importLanguage）也复制到这里
// 之后只扫这一个目录，内置与导入完全同构
// 拿不到外部私有目录（存储没挂载 / 被系统拒绝）时退回内部的 files/language

// 缺键 / 未知 key 一律返回 LanguageManager.MISSING_STRING
// 没有系统语言检测：兜底就是硬编码的 zh_CN
public final class Lang {

    private static final String ASSET_DIR = "language";

    // 内部语言目录（filesDir 下的子目录名）
    private static final String LANGUAGE_DIR = "language";

    // 硬编码兜底语言
    private static final String DEFAULT_CODE = "zh_CN";

    private static Context context;

    private static LanguageManager manager;

    private static String current_code = DEFAULT_CODE;

    private Lang() {
    }

    // 初始化：铺内置语言 -> 建 manager（构造时扫目录）-> 载入指定语言
    // language_code 为空或载入失败时退回 DEFAULT_CODE
    public static synchronized void init(Context app_context, String language_code) {
        context = app_context.getApplicationContext();

        File directory = languageDirectory();
        seedBuiltInLanguages(directory);

        manager = new LanguageManager(directory);

        if (!select(language_code)) {
            select(DEFAULT_CODE);
        }
    }

    // 切到指定语言；载入失败返回 false（字典保持原样）
    public static synchronized boolean select(String language_code) {
        String code = (language_code == null || language_code.isEmpty())
                ? DEFAULT_CODE
                : language_code;

        if (manager == null) {
            return false;
        }

        if (manager.loadLanguage(code)) {
            current_code = code;
            return true;
        }

        // 语言目录里那份读不动（被改坏 / 少了 content）：先从 assets 复原再试一次
        if (restoreBuiltInLanguage(code) && manager.loadLanguage(code)) {
            current_code = code;
            return true;
        }

        // 兜底：硬编码的默认语言
        if (!DEFAULT_CODE.equals(code) && manager.loadLanguage(DEFAULT_CODE)) {
            current_code = DEFAULT_CODE;
            return true;
        }

        return false;
    }

    // 重新载入当前语言（外部文件改动后调用）
    public static synchronized boolean reload() {
        return select(current_code);
    }

    // 外部导入语言：把外部 json 复制到内部语言目录，之后就能在 languages() 里看到它
    // （导入用的 UI 稍后接：选到文件后调用它，再 reload() 重画即可）
    public static synchronized boolean importLanguage(File source_file) {
        if (manager == null) {
            return false;
        }

        return manager.importLanguage(source_file, null);
    }

    // 取文案：缺键 / 未知返回 MISSING_STRING
    public static synchronized String get(String id) {
        if (manager == null) {
            return LanguageManager.MISSING_STRING;
        }

        return manager.getString(id);
    }

    // 带占位符：Lang.f("toast.item_count", 3) 对应 "共 %1 项"
    //
    // 占位符统一用 Qt 的写法 %1 %2 %3（QString::arg）：桌面端与 Android 端共用同一份文案，
    // 两边的字符串文件才能逐字一致。Java 的 String.format 要的是 %1$s，所以这里把 %N
    // 补成 %N$s（%s 对数字和字符串都吃）；已经是 %N$s / %N$d 的旧写法保持不动。
    public static synchronized String f(String id, Object... args) {
        if (manager == null) {
            return LanguageManager.MISSING_STRING;
        }

        String template = manager.getString(id);

        if (LanguageManager.MISSING_STRING.equals(template)) {
            return template;
        }

        try {
            return String.format(Locale.getDefault(), toJavaPlaceholders(template), args);
        } catch (RuntimeException error) {
            return template;
        }
    }

    // %1 -> %1$s（负数/已带 $ 的写法不动）
    private static String toJavaPlaceholders(String template) {
        if (template.indexOf('%') < 0) {
            return template;
        }

        return template.replaceAll("%(\\d+)(?!\\$)", "%$1\\$s");
    }

    // 当前语言代码（zh_CN）
    public static synchronized String current() {
        return current_code;
    }

    // 当前语言的显示名（取自语言文件里的 name）
    public static synchronized String currentName() {
        if (manager == null) {
            return current_code;
        }

        String name = manager.getLanguageName();

        return name == null || name.isEmpty() ? current_code : name;
    }

    // 可用语言列表（= 内部语言目录下的 *.json 文件名）
    public static synchronized List<String> languages() {
        if (manager == null) {
            return new ArrayList<>();
        }

        return new ArrayList<>(manager.getLanguagesList());
    }

    public static synchronized String lastError() {
        return manager == null ? "" : manager.getLastError();
    }

    private static File languageDirectory() {
        File external = context.getExternalFilesDir(null);

        if (external != null) {
            return new File(external, LANGUAGE_DIR);
        }

        // 存储没挂载 / 被系统拒绝：退回内部目录 至少内置语言还能用
        return new File(context.getFilesDir(), LANGUAGE_DIR);
    }

    // 把内置语言铺到语言目录
    // 1. 语言目录为空（或者少了这个语言文件）-> 从 assets 整个复制过去
    // 2. 文件在 但少了内置里有的键 -> 只把缺的键补进去
    private static void seedBuiltInLanguages(File directory) {
        if (!directory.isDirectory() && !directory.mkdirs()) {
            return;
        }

        try {
            String[] names = context.getAssets().list(ASSET_DIR);

            if (names == null) {
                return;
            }

            for (String name : names) {
                if (!name.endsWith(".json")) {
                    continue;
                }

                File target = new File(directory, name);

                if (!target.isFile()) {
                    copyAsset(ASSET_DIR + "/" + name, target);
                    continue;
                }

                fillMissingKeys(ASSET_DIR + "/" + name, target);
            }
        } catch (IOException error) {
            // assets 里没有语言目录时忽略
        }
    }

    private static void fillMissingKeys(String asset_path, File target) {
        if (context == null) {
            return;
        }

        try {
            String asset_text = new String(
                    readAll(context.getAssets().open(asset_path)), StandardCharsets.UTF_8);
            String local_text = new String(Files.readAllBytes(target.toPath()), StandardCharsets.UTF_8);

            JSONArray asset_items = new JSONObject(asset_text).optJSONArray("text");
            JSONObject local_json = new JSONObject(local_text);
            JSONArray local_items = local_json.optJSONArray("text");

            if (asset_items == null || local_items == null) {
                return;
            }

            Set<String> have = new HashSet<>();

            for (int i = 0; i < local_items.length(); i++) {
                JSONObject item = local_items.optJSONObject(i);

                if (item != null) {
                    have.add(item.optString("id", ""));
                }
            }

            boolean changed = false;

            for (int i = 0; i < asset_items.length(); i++) {
                JSONObject item = asset_items.optJSONObject(i);

                if (item == null) {
                    continue;
                }

                String id = item.optString("id", "");

                if (id.isEmpty() || have.contains(id)) {
                    continue;
                }

                local_items.put(item);
                changed = true;
            }

            if (changed) {
                Files.write(target.toPath(),
                        (local_json.toString(4) + "\n").getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException error) {
            // 读不动就保持原样
        } catch (JSONException error) {
            // 本地那份不是合法 json：不动它 载入失败时 select 会从 assets 复原
        }
    }

    // 把某个内置语言从 assets 整个复制回语言目录（语言目录里那份读不动时用）
    private static boolean restoreBuiltInLanguage(String code) {
        if (context == null || code == null || code.isEmpty()) {
            return false;
        }

        File directory = languageDirectory();

        if (!directory.isDirectory() && !directory.mkdirs()) {
            return false;
        }

        String file_name = code + ".json";

        try {
            String[] names = context.getAssets().list(ASSET_DIR);

            if (names == null) {
                return false;
            }

            for (String candidate : names) {
                if (candidate.equals(file_name)) {
                    copyAsset(ASSET_DIR + "/" + file_name, new File(directory, file_name));

                    return true;
                }
            }
        } catch (IOException error) {
            return false;
        }

        return false;
    }

    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        byte[] chunk = new byte[8192];
        int read = input.read(chunk);

        while (read > 0) {
            buffer.write(chunk, 0, read);
            read = input.read(chunk);
        }

        return buffer.toByteArray();
    }

    private static void copyAsset(String asset_path, File target) {
        try (InputStream input = context.getAssets().open(asset_path)) {
            Files.copy(input, target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException error) {
            // 单个语言失败不影响其它语言
        }
    }
}
