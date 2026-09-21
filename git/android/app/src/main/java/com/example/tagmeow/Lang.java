package com.example.tagmeow;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

// 语言门面：UI 只调用 Lang.get("key") / Lang.f("key", args)

// 语言文件只放一个位置：<filesDir>/language/*.json
// - 内置语言（assets/language/*.json）第一次启动时铺进去
// - 外部导入的语言（Lang.importLanguage）也复制到这里
// 之后只扫这一个目录，内置与导入完全同构

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
        return new File(context.getFilesDir(), LANGUAGE_DIR);
    }

    // 把内置语言铺到内部目录
    // 铺完之后所有语言都从内部目录读 内置 / 导入同构
    // 判定标准是「和 assets 里的内容一样不一样」而不是「文件在不在」：
    // 只按存在性铺的话 加了新文案的版本装到老设备上 手里那份 json 永远不会更新
    // 新 key 会一直显示 MISSING_STRING（同步进度那几条就踩过这个坑）
    // 代价：同名的外部导入语言会被内置的盖掉 —— 内置语言由应用负责维护 这是有意的
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

                if (target.isFile() && sameAsAsset(ASSET_DIR + "/" + name, target)) {
                    continue;
                }

                copyAsset(ASSET_DIR + "/" + name, target);
            }
        } catch (IOException error) {
            // assets 里没有语言目录时忽略
        }
    }

    // 内部那份和 assets 里的内置语言是不是同一份内容
    // 比内容而不是记版本号：不会出现「改了文案忘了把版本号加一」这种坑
    private static boolean sameAsAsset(String asset_path, File target) {
        if (context == null) {
            return false;
        }

        try (InputStream input = context.getAssets().open(asset_path)) {
            return Arrays.equals(readAll(input), Files.readAllBytes(target.toPath()));
        } catch (IOException error) {
            return false;
        }
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
