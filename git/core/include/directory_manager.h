#pragma once

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

// 统一使用 UTF-8
// DirectoryConfigManager 从JSON中加载管理授权的目录列表 目标路径没有文件会创建对应格式文件
// 保存所有授权路径到文件 只在添加文件进行有效性判断 后续可添加文件是否重命名检查处理
// 格式示例：{"managed_dirs": ["C:\\Users\\h\\Desktop\\test"]}
// 确保路径的合法性
// 自己调用时保证线程安全

struct Directory
{
    std::string original_path_;            // 原始路径
    std::filesystem::path canonical_path_; // 规范化后的绝对路径
    bool is_valid_;                        // 目录是否有效
};

class DirectoryConfigManager
{
public:
    DirectoryConfigManager() = default;
    DirectoryConfigManager(const std::filesystem::path &config_path_utf8 = "./config/path.json");
    ~DirectoryConfigManager();

    // 禁止拷贝
    DirectoryConfigManager &operator=(const DirectoryConfigManager &) = delete;
    DirectoryConfigManager &operator=(DirectoryConfigManager &&) = delete;

    bool loadFromFile(const std::filesystem::path &file_path_utf8);
    bool saveToFile();
    void clearInvalidPath();

    bool addDirectory(const std::filesystem::path &utf8_path);
    bool removeDirectory(const std::filesystem::path &utf8_path);
    bool isPathAllowed(const std::filesystem::path &utf8_path) const;


    const std::string &getLastError() const;
    std::filesystem::path getConfigPath() const;
    const std::vector<Directory> &getDirectories() const;
    const std::filesystem::path &getLastValidDir() const;
    const std::vector<std::filesystem::path> getValidDirList() const;

private:
    std::filesystem::path config_path_;
    std::vector<Directory> managed_directories_;
    mutable std::string error_string_;
    static inline const std::filesystem::path empty_ = "";

    // 只检查路径是否存在是目录有读写权限是否存在 不修改任何内容
    bool isDirectoryValid(const std::filesystem::path &path) const;
    // 将目录转换为标准存储格式UTF-8 + '/' 分隔
    std::filesystem::path formatPath(std::filesystem::path path);
    // 判断路径是否是某路径的子目录
    bool isPathUnderRoot(const std::filesystem::path &path, const std::filesystem::path &root) const;
};