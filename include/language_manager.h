#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <nlohmann/json.hpp>

// 统一使用 UTF-8
// LanguageManager 从给定的目录下查找语言文件（JSON格式）并提供根据字符串ID获取翻译文本的功能
// 没有提供 ID 分配的方法 所以 JSON 和代码中的 string 应该相等
// 格式示例 ：{"name": "zh_CN","version": "1.0","text": [{ "id": "test.button", "str": "测试" }]},
// 热加载根据需求考虑是否实现 多线程懒得处理了 调用时保证线程安全

class LanguageManager
{
public:
    LanguageManager() = delete;
    LanguageManager(const std::filesystem::path &language_directory_utf8 = "./language");
    ~LanguageManager();

    // 禁止拷贝
    LanguageManager(const LanguageManager &) = delete;
    LanguageManager &operator=(const LanguageManager &) = delete;

    // 加载指定语言
    bool loadLanguage(const std::string &language_name_utf8);
    // 从指定目录加载语言列表
    bool loadLanguageList(const std::filesystem::path &directory_path_utf8);

    const std::string &getString(const std::string &id_utf8) const;
    const std::string &getLanguageName() const;
    const std::string &getLastError() const;
    const std::vector<std::string> &getLanguagesList() const;

private:
    std::string language_name_;
    std::vector<std::string> language_list_;
    std::filesystem::path language_directory_path_;
    std::string missing_string_ = "MISSING_STRING";
    mutable std::string error_string_;

    // 字符串 ID -> 翻译文本
    std::unordered_map<std::string, std::string> language_dictionary_;
};
