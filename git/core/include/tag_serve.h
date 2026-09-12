#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <mutex>

#include "file_database.h"
#include "tag_manager.h"

// TagServe 数据层内部统一维护 TagLibrary 和 TagFileManager 和 FileDatabase 的关系并正确处
// TagServe 储存层内存内部维护与数据层的关系并正确处
// TagServe 应该保证线程安全 对外提供只提供稳定方便安全的接口

// TagFileManager tag_file_ 内部采用默认模式 即 TagServe::convertMode 会修改默认模式
// 默认模式应该在上层调用 getDefaultMode 持久存储 构造时传入

class TagServe
{
public:
    // root_list 由 DirectoryConfigManager 保证有效性
    TagServe(const std::vector<std::filesystem::path> &root_list_utf8 = {}, TagFileManager::StoreMode default_mode = TagFileManager::StoreMode::Sidecar, std::filesystem::path tag_path_utf8 = "./config/tag.json", std::filesystem::path db_path_utf8 = "./config/index.db");
    ~TagServe();

    // 禁止拷贝
    TagServe(const TagServe &) = delete;
    TagServe &operator=(const TagServe &) = delete;

    // 数据库同步更新添加此目录信息 上层调用时保证路径有效性
    bool addRoot(std::filesystem::path root_path_utf8);
    // 数据库同步更新删除此目录信息 上层调用时保证路径有效性
    bool removeRoot(std::filesystem::path root_path_utf8);
    // 替换根目录列表 并同步更新数据库 删除旧目录数据插入新目录数据
    bool reLoadRoot(std::filesystem::path root_list_utf8);
    bool reLoadRoot(std::vector<std::filesystem::path> root_list_utf8);
    // 更换数据库文件路径 但不自动同步文件系统 需要手动调用 updateRoots
    bool reLoadDB(std::filesystem::path db_path_utf8);
    

    // 标签库
    bool reLoadTag(std::filesystem::path tag_path_utf8);
    // 需要手动保存同步到文件
    bool saveTag();
    bool addTag(const std::string &type, const std::string &tag);
    bool addTag(const std::string &type, const std::vector<std::string> &tags);
    bool addType(const std::string &type, std::string &color);
    bool removeTag(const std::string &tag);
    bool removeTag(const std::vector<std::string> &tags);
    bool removeType(const std::string &type);
    bool removeType(const std::vector<std::string> &types);
    bool renameTag(const std::string &old_tag, const std::string &new_tag);
    bool renameType(const std::string &old_type, const std::string &new_type);
    bool setTypeColor(const std::string &type, const std::string &new_color);
    bool setTagType(const std::string &tag, const std::string &new_type);

    std::string getColorByType(const std::string &type) const;
    std::string getColorByTag(const std::string &tag) const;
    std::string getTypeOfTag(const std::string &tag) const;
    std::unordered_map<std::string, std::vector<std::string>> getTypeTag() const;
    std::unordered_map<std::string, std::string> getTypeColor() const;

    // 添加标签
    bool addFileTag(const std::filesystem::path &file_path_utf8, const std::string &tag);
    bool addFileTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags);
    bool removeFileTag(const std::filesystem::path &file_path_utf8, const std::string &tag);
    bool removeFileTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags);
    // 转换 root 目录列表内的全部文件 如果某文件失败将跳过该文件 如果 keep_old 为 false 只有在全部文件转换成功才删除 如果成功会将默认模式设置为 to_mode
    bool convertMode(TagFileManager::StoreMode from_mode, TagFileManager::StoreMode to_mode, bool keep_old = false);
    void setDefaultMode(const TagFileManager::StoreMode mode);
    const TagFileManager::StoreMode getDefaultMode() const;

    // 数据库
    // 从默认目录列表获取数据更新数据库 root_list_
    bool updateRoots();
    bool updateFile(const std::filesystem::path &file_path_utf8);
    bool removeFile(const std::filesystem::path &path_utf8);
    std::vector<table::FileInfo> searchByTags(const FileDatabase::SearchOptions &opts) const;
    std::optional<table::FileInfo> getFileInfo(const std::filesystem::path &path) const;

    // 最近一次文件标签操作后的真实路径
    // Filename 模式下加/删标签会重命名文件 该值与传入路径可能不同(数据库已按此路径同步)
    const std::filesystem::path &getLastFilePath() const;

    // TagServe 错误信息
    std::string getLastError() const;
    // FileDatabase 错误信息
    std::string getDBError() const;
    // TagLibrary 错误信息
    std::string getTagError() const;
    // TagFileManager 错误信息
    std::string getFileError() const;

private:
    // 内部无锁版本: 把单个文件当前状态写入数据库(updateFile 加锁后调用 避免重入死锁)
    bool syncFileToDBNoLock(const std::filesystem::path &file_path_utf8);
    // 内部无锁版本: 用当前 root_list_ 重建数据库索引(模式转换改名后调用)
    bool rebuildRootsNoLock();
    // 内部无锁版本: 标签写入成功后同步数据库(以磁盘实际状态判断是否发生改名)
    void syncAfterTagWriteNoLock(const std::filesystem::path &old_path, const std::filesystem::path &predicted_path);

    FileDatabase db_;
    TagLibrary tag_list_;
    TagFileManager tag_file_;
    std::vector<std::filesystem::path> root_list_;
    std::filesystem::path last_file_path_; // 最近一次文件标签操作后的真实路径
    mutable std::string error_string_;
    mutable std::mutex mutex_;
};