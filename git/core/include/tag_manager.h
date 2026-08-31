#pragma once

#include <regex>
#include <cctype>
#include <random>
#include <optional>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

// 统一使用 UTF-8
// TagLibrary addType 保证 type 全局唯一 addTag 保证 tag 全局唯一
// TagFileManager 管理文件的 tag 信息处理 文件名标记 tag 和 额外文件存储 tag 两种格式理
// removeTag/removeType/renameTag 只操作标签库 不联动文件
// TagFileManager Filename 模式格式示例: 文件{[标签1,标签2]}.txt
// TagFileManager SidecarF 模式格式示例: ./.tag/文件.json 同级目录下的.tag文件内 json：{tags:"标签1,标签2"}
// TagFileManager 除了根目录和.tag目录外 所有目录和文件都能添加标签 但是 rename 目录名会导致路径问题 所有目录只能用 Sidecar 模式
// 自己调用时保证线程安全

class TagLibrary
{
public:
    TagLibrary(const std::filesystem::path &file_path_utf8 = "./config/tag.json");
    ~TagLibrary();

    // 禁止拷贝
    TagLibrary &operator=(const TagLibrary &) = delete;
    TagLibrary &operator=(TagLibrary &&) = delete;

    bool loadTagsFromFile(const std::filesystem::path &file_path_utf8);
    bool saveTagsToFile() const;

    static bool isValidHexColor(const std::string &str);

    bool addTag(const std::string &tag, const std::string &type);
    bool addType(const std::string &type, std::string &color);
    bool removeTag(const std::string &tag);
    bool removeType(const std::string &type);
    bool renameTag(const std::string &old_tag, const std::string &new_tag);
    bool renameType(const std::string &old_type, const std::string &new_type);
    bool hasTag(const std::string &tag) const;
    bool hasType(const std::string &type) const;
    void clearInvalidTag();

    bool setTypeColor(const std::string &type, const std::string &new_color);
    bool setTagType(const std::string &tag, const std::string &new_type);

    std::string getColorByType(const std::string &type) const;
    std::string getTypeOfTag(const std::string &tag) const;
    std::vector<std::string> getAllTypeNames() const;
    std::vector<std::string> getAllTagNames() const;
    const std::unordered_map<std::string, std::vector<std::string>> &getTypeTag() const;
    const std::unordered_map<std::string, std::string> &getTypeColor() const;
    const std::filesystem::path &getLoadPath() const;
    const std::string &getLastError() const;

    std::vector<std::string> autoComplete(const std::string &prefix) const;

private:
    std::string load_path_;
    std::filesystem::path config_path_;
    std::unordered_map<std::string, std::vector<std::string>> type_tags_;
    std::unordered_map<std::string, std::string> type_color_;
    mutable std::string error_string_;
};

class TagFileManager
{
public:
    enum class StoreMode
    {
        Filename,
        Sidecar
    };

    TagFileManager(const StoreMode default_mode = StoreMode::Sidecar);
    ~TagFileManager();

    void setDefaultMode(const StoreMode mode);
    const StoreMode &getDefaultMode() const;
    const std::string &getLastError() const;

    // 全部用成员变量的默认模式
    bool addTag(const std::filesystem::path &file_path_utf8, const std::string &tag);
    bool removeTag(const std::filesystem::path &file_path_utf8, const std::string &tag);
    bool removeTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags);
    // 单个文件模式转换 如果 from_mode == to_mode 尝试删除 from_mode 模式
    bool convertMode(const std::filesystem::path &file_path_utf8, StoreMode from_mode, StoreMode to_mode, bool keep_old = false);
    // 删除文件在指定模式下存储的标签（Filename 重命名 / Sidecar 删文件）
    bool removeModeTags(const std::filesystem::path &file_path_utf8, StoreMode mode);
    std::vector<std::string> extractTags(const std::filesystem::path &file_path_utf8, StoreMode mode) const;
    std::vector<std::string> extractTags(const std::filesystem::path &file_path_utf8) const;

    // 根据根目录构建侧车文件的完整路径
    static std::filesystem::path buildSidecarPath(const std::filesystem::path &file_path_utf8);
    // 构建"无标签"侧车文件路径 先去除文件名中的标签块再定位侧车
    static std::filesystem::path buildCleanSidecarPath(const std::filesystem::path &file_path_utf8);

private:
    StoreMode default_mode_;
    mutable std::string error_string_;

    // 从文件名中解析出标签列表
    static std::vector<std::string> parseFromFilename(const std::filesystem::path &file_name);
    // 将基础文件名和标签列表组合成带标签的新文件名 不带扩展名
    static std::string formatFilenameWithTags(const std::string &file_name, const std::vector<std::string> &tags);
    // 从文件名中移除所有标签块返回纯文件名 不带扩展名
    static std::string removeTagsFromFilename(const std::string &file_name);
    // 从文件路径中去除文件名中的标签块返回纯净路径
    static std::filesystem::path removeFilenameTagsPath(const std::filesystem::path &path);
    // 读取侧车文件中的标签列表
    static bool readSidecar(const std::filesystem::path &sidecar_path_utf8, std::vector<std::string> &tags);
    // 将标签列表写入侧车文件
    static bool writeSidecar(const std::filesystem::path &sidecar_path_utf8, const std::vector<std::string> &tags);
    // 将标签按指定模式写入文件
    static bool writeTagsToFile(const std::filesystem::path &file_path, const std::vector<std::string> &tags, StoreMode mode);
};
