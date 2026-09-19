package com.example.tagmeow;

import java.util.Objects;

// 使用 root_id + relative_path 定位文件
// 不直接保存 Android Uri

// relative_path 统一使用 '/' 分隔
// relative_path 不带首尾 '/'
// relative_path 为空字符串时表示 root 目录本身

public final class FileRef {

    // 数据库身份键分隔符 使用单元分隔符避免与 root_id / 路径中的 ':' '/' 冲突
    private static final char KEY_SEPARATOR = '\u001F';
    // 对应某个受管理根目录
    private final String root_id;
    // 相对 root 的路径 统一使用 '/' 分隔
    private final String relative_path;

    public FileRef(String root_id, String relative_path) {

        this.root_id = Objects.requireNonNull(root_id);
        this.relative_path = normalize(Objects.requireNonNull(relative_path));
    }

    public String getRootId() {
        return root_id;
    }

    public String getRelativePath() {
        return relative_path;
    }

    // 是否为 root 目录本身
    public boolean isRoot() {
        return relative_path.isEmpty();
    }

    // 返回最后一段名称 root 自身返回空字符串
    public String getName() {
        if (relative_path.isEmpty()) {
            return "";
        }

        int slash = relative_path.lastIndexOf('/');
        return slash < 0 ? relative_path : relative_path.substring(slash + 1);
    }

    // 返回父目录相对路径 root 自身返回空字符串
    public String getParentPath() {
        int slash = relative_path.lastIndexOf('/');
        return slash < 0 ? "" : relative_path.substring(0, slash);
    }

    // 返回父目录引用 已经是 root 时返回自身
    public FileRef getParent() {
        if (isRoot()) {
            return this;
        }

        return new FileRef(root_id, getParentPath());
    }

    // 生成子项引用 name 不允许为空 也不允许包含 '/'
    public FileRef child(String name) {
        Objects.requireNonNull(name);

        if (name.isEmpty() || name.indexOf('/') >= 0) {
            throw new IllegalArgumentException("invalid child name: " + name);
        }

        return new FileRef(root_id, relative_path.isEmpty() ? name : relative_path + "/" + name);
    }

    // 数据库身份键 同一个 root_id + relative_path 必然得到同一个键
    public String key() {
        return root_id + KEY_SEPARATOR + relative_path;
    }

    // 规范化相对路径 统一分隔符 去掉首尾与重复的 '/'
    public static String normalize(String path) {
        if (path == null || path.isEmpty()) {
            return "";
        }

        String unified = path.replace('\\', '/');
        StringBuilder builder = new StringBuilder(unified.length());

        boolean last_was_slash = false;
        for (int i = 0; i < unified.length(); i++) {
            char c = unified.charAt(i);

            if (c == '/') {
                if (builder.length() == 0 || last_was_slash) {
                    continue;
                }
                last_was_slash = true;
                builder.append(c);
            } else {
                last_was_slash = false;
                builder.append(c);
            }
        }

        if (builder.length() > 0 && builder.charAt(builder.length() - 1) == '/') {
            builder.setLength(builder.length() - 1);
        }

        return builder.toString();
    }

    @Override
    public boolean equals(Object obj) {
        if (this == obj) {
            return true;
        }

        if (!(obj instanceof FileRef)) {
            return false;
        }

        FileRef other = (FileRef) obj;

        return root_id.equals(other.root_id)
                && relative_path.equals(other.relative_path);
    }

    @Override
    public int hashCode() {
        return Objects.hash(root_id, relative_path);
    }

    @Override
    public String toString() {
        return root_id + ":" + relative_path;
    }
}