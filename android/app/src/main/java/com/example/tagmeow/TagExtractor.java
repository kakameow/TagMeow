package com.example.tagmeow;

import java.util.List;

// FileDatabase 扫描文件时获取标签的回调
@FunctionalInterface
public interface TagExtractor {
    List<String> extract(FileRef file);
}
