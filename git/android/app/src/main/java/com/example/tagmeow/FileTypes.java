package com.example.tagmeow;

import java.util.Locale;

// 文件类型判断：只看扩展名（不去查 MIME，也不读文件内容）
//
// 单独拎出来是为了能在 JVM 单元测试里跑：
// 界面用它决定显示哪张矢量图，图片 / 视频还会再试着取系统缩略图，
// 取不到就退回这里的图标
public final class FileTypes {

    private static final String[] IMAGE = {
            ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".heic", ".heif", ".avif", ".svg"};

    private static final String[] VIDEO = {
            ".mp4", ".mkv", ".avi", ".mov", ".webm", ".3gp", ".m4v", ".flv", ".wmv", ".mpg", ".mpeg", ".ts"};

    private static final String[] AUDIO = {
            ".mp3", ".wav", ".flac", ".ogg", ".m4a", ".aac", ".opus", ".wma", ".amr"};

    private static final String[] TEXT = {
            ".txt", ".md", ".json", ".xml", ".java", ".kt", ".kts", ".cpp", ".cc", ".c", ".h", ".hpp",
            ".py", ".js", ".ts", ".html", ".htm", ".css", ".csv", ".log", ".yml", ".yaml", ".ini",
            ".sh", ".bat", ".gradle", ".properties", ".toml"};

    private FileTypes() {
    }

    public static boolean isImage(String file_name) {
        return endsWithAny(file_name, IMAGE);
    }

    public static boolean isVideo(String file_name) {
        return endsWithAny(file_name, VIDEO);
    }

    public static boolean isAudio(String file_name) {
        return endsWithAny(file_name, AUDIO);
    }

    public static boolean isText(String file_name) {
        return endsWithAny(file_name, TEXT);
    }

    // 图片和视频能取缩略图，其他类型只能按图标显示
    public static boolean hasThumbnail(String file_name) {
        return isImage(file_name) || isVideo(file_name);
    }

    private static boolean endsWithAny(String file_name, String[] suffixes) {
        if (file_name == null) {
            return false;
        }

        String lower = file_name.toLowerCase(Locale.ROOT);

        for (String suffix : suffixes) {
            if (lower.endsWith(suffix)) {
                return true;
            }
        }

        return false;
    }
}
