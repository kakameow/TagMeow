#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "tag_manager.h"

struct ConfigLoader
{
    ConfigLoader();
    ~ConfigLoader();

    // 加载配置覆盖默认值 字段缺失/类型错误时使用默认值 文件不存在返回 false
    bool loadConfig(std::filesystem::path path_utf8 = "./config/config.json");
    // 保存配置(文件/目录不存在时自动创建) 成功返回 true
    bool saveConfig(std::filesystem::path path_utf8 = "./config/config.json");

    std::string version_ = "beta";
    std::string default_language_ = "zh-cn";
    TagFileManager::StoreMode tag_mode_ = TagFileManager::StoreMode::Sidecar;
    std::chrono::minutes server_waiting_time_ = std::chrono::minutes(5);
    std::filesystem::path download_path_ = "./download";
    std::uint16_t broadcast_port_ = 11451;          // UDP_DEFAULT_PORT
    std::string broadcast_magic_word_ = "0x114514"; // UDP_DEFAULT_MAGIC

    mutable std::string error_string_;
};

#endif // CONFIG_LOADER_H
