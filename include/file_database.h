#pragma once

#include <set>
#include <chrono>
#include <sstream>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <filesystem>
#include <sqlite3.h>

// 统一使用 UTF-8
// FileDatabase 数据层从磁盘获取数据 不经过其他模块
// 自己调用时保证线程安全

namespace table
{
    // 文件基础信息表
    struct FileInfo
    {
        int file_id_;                   // 自增主键
        std::string path_;              // 绝对路径 UTF-8
        std::string rel_path_;          // 相对路径 UTF-8
        int64_t file_mtime_;            // 文件修改时间
        int64_t file_size_;             // 文件大小
        int64_t sidecar_mtime_;         // 侧车文件修改时间
        std::vector<std::string> tags_; // 标签列表
        int file_version_;              // 乐观锁版本
        int64_t last_refresh_time_;     // 最后刷新时间
    };

    // 文件标签表
    struct TagEntry
    {
        std::string tag_; // 标签名称
        int file_id_;     // 关联的文件ID
    };
}

class FileDatabase
{
public:
    // 搜索条件
    struct SearchOptions
    {
        // 排除的标签
        std::vector<std::string> exclude_;
        // 只能包含的标签为空则不限
        std::vector<std::string> only_;
        // 至少包含其中一个
        std::vector<std::string> include_;
    };

    explicit FileDatabase(const std::filesystem::path &db_path_utf8 = "./config/index.db");
    ~FileDatabase();

    // 禁止拷贝
    FileDatabase &operator=(const FileDatabase &) = delete;
    FileDatabase &operator=(FileDatabase &&) = delete;

    bool reload(const std::filesystem::path &db_path_utf8);
    // 初始化数据库
    bool initSchema();
    // 将单个目录内所有文件信息更新同步到数据库
    bool updateDirectory(const std::filesystem::path &path_utf8, std::function<std::vector<std::string>(const std::filesystem::path &)> tag_extractor);
    // 更新单个文件 由上层在文件修改后调用
    bool updateFile(const table::FileInfo &info);
    // 从数据库移除文件记录
    bool removeFile(const std::filesystem::path &path_utf8);
    // 从数据库移除整个目录的所有记录
    bool removeDirectory(const std::filesystem::path &dir_path_utf8);
    bool clearRepeat();
    // 清除无效记录：删除 files 表中磁盘上已不存在的文件记录 以磁盘为准 只读磁盘不改磁盘
    // 注意：若某个磁盘/分区临时未挂载 std::filesystem::exists 会返回 false 该记录会被当作失效删除
    bool cleanupInvalid();

    // 按标签搜索文件
    std::vector<table::FileInfo> searchByTags(const SearchOptions &opts) const;
    // 获取单个文件信息
    std::optional<table::FileInfo> getFileInfo(const std::filesystem::path &path) const;
    const std::string &getLastError() const;

private:
    sqlite3 *db_ = nullptr;
    std::filesystem::path db_path_;
    mutable std::string error_string_;
};
