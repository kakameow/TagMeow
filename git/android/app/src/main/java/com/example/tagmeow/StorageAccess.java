package com.example.tagmeow;

import java.io.IOException;
import java.util.List;

// Android 存储薄封装
// 负责：
// - FileRef 与 Android SAF Uri 的对应
// - 文件/目录遍历
// - 文件读写
// - 创建目录
// - 删除
// - 重命名
// - 文件属性
// 不负责：
// - 标签格式
// - SQLite
// - TagLibrary
// - 标签规则

// - FileRef.relative_path 为空时表示 root 目录本身
// - 所有实现都不应该改变上层业务模块的职责

public interface StorageAccess {

    // 注册一个受管理根目录
    // 使 FileRef.root_id 能够被解析为实际存储位置

    // locator 由实现自行解释：
    // - SAF 实现为 tree Uri 字符串
    // - 本地实现为绝对路径

    // 重复注册同一个 root_id 视为覆盖
    void addRoot(String root_id, String locator);
    // 注销一个受管理根目录
    void removeRoot(String root_id);
    // 判断文件或目录是否存在
    boolean exists(FileRef ref);
    // 判断指定引用是否为目录
    boolean isDirectory(FileRef ref);
    // 列出目录子项
    List<FileRef> listChildren(FileRef directory);
    // 读取全部文件内容
    byte[] readAll(FileRef ref) throws IOException;
    // 写入全部文件内容
    void writeAll(FileRef ref, byte[] data, boolean atomic) throws IOException;
    // 递归创建目录 已存在时返回 true
    boolean createDirectories(FileRef directory);
    // 删除文件或目录
    boolean delete(FileRef ref);
    // 重命名文件或目录 只提供新名称 不改变父目录
    boolean rename(FileRef ref, String new_name);
    // 获取最后修改时间 无法获取时返回 0
    long lastModified(FileRef ref);
    // 获取文件大小 无法获取时返回 0
    long size(FileRef ref);
    // 解析 FileRef 在存储层的定位串
    // - SAF 实现返回 document Uri 字符串
    // - 本地实现返回绝对路径
    // 不存在时返回 null
    String locatorOf(FileRef ref);
    // 根据父目录和名称生成子项引用
    FileRef childOf(FileRef directory, String name);
}