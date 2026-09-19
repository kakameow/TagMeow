#include "config_loader.h"

#include <fstream>

#include <nlohmann/json.hpp>

ConfigLoader::ConfigLoader()
{
    if (!loadConfig())
    {
        saveConfig();
    }
}

ConfigLoader::~ConfigLoader()
{
    saveConfig();
}

bool ConfigLoader::loadConfig(std::filesystem::path path_utf8)
{
    error_string_.clear();

    if (!std::filesystem::exists(path_utf8))
    {
        error_string_ = "[warning] file does not exist: " + path_utf8.string();
        return false;
    }

    try
    {
        std::ifstream file(path_utf8);
        if (!file.is_open())
        {
            error_string_ = "[warning] file cannot be opened: " + path_utf8.string();
            return false;
        }

        nlohmann::json config_json;
        file >> config_json;

        // 保留完整 JSON
        raw_json_ = config_json;

        bool has_error = false;
        std::string field_errors;

        if (config_json.contains("Version"))
        {
            if (config_json["Version"].is_string())
            {
                version_ = config_json["Version"].get<std::string>();
            }
            else
            {
                field_errors += "- Version: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("DefaultLanguage"))
        {
            if (config_json["DefaultLanguage"].is_string())
            {
                default_language_ = config_json["DefaultLanguage"].get<std::string>();
            }
            else
            {
                field_errors += "- DefaultLanguage: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("TagMode"))
        {
            if (config_json["TagMode"].is_string())
            {
                std::string mode = config_json["TagMode"].get<std::string>();
                if (mode == "Sidecar")
                {
                    tag_mode_ = TagFileManager::StoreMode::Sidecar;
                }
                else if (mode == "Filename" || mode == "Embedded")
                {
                    tag_mode_ = TagFileManager::StoreMode::Filename;
                }
            }
            else
            {
                field_errors += "- TagMode: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("ServerWaitingTime"))
        {
            if (config_json["ServerWaitingTime"].is_number_integer())
            {
                int wait_minutes = config_json["ServerWaitingTime"].get<int>();
                if (wait_minutes >= 0)
                {
                    server_waiting_time_ = std::chrono::minutes(wait_minutes);
                }
                else
                {
                    field_errors += "- ServerWaitingTime: cannot be negative (" + std::to_string(wait_minutes) + ")\n";
                    has_error = true;
                }
            }
            else
            {
                field_errors += "- ServerWaitingTime: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("DownloadPath"))
        {
            if (config_json["DownloadPath"].is_string())
            {
                download_path_ = config_json["DownloadPath"].get<std::string>();
            }
            else
            {
                field_errors += "- DownloadPath: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("BroadcastPort"))
        {
            if (config_json["BroadcastPort"].is_number_integer())
            {
                int port = config_json["BroadcastPort"].get<int>();
                if (port > 0 && port <= 65535)
                {
                    broadcast_port_ = static_cast<std::uint16_t>(port);
                }
                else
                {
                    field_errors += "- BroadcastPort: out of range 1-65535 (" + std::to_string(port) + ")\n";
                    has_error = true;
                }
            }
            else
            {
                field_errors += "- BroadcastPort: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("BroadcastMagicWord"))
        {
            if (config_json["BroadcastMagicWord"].is_string())
            {
                broadcast_magic_word_ = config_json["BroadcastMagicWord"].get<std::string>();
            }
            else
            {
                field_errors += "- BroadcastMagicWord: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("FontSize"))
        {
            if (config_json["FontSize"].is_number_integer())
            {
                int fs = config_json["FontSize"].get<int>();
                if (fs >= 6 && fs <= 48)
                {
                    font_size_ = fs;
                }
                else
                {
                    field_errors += "- FontSize: out of range 6-48 (" + std::to_string(fs) + ")\n";
                    has_error = true;
                }
            }
            else
            {
                field_errors += "- FontSize: type error using default value\n";
                has_error = true;
            }
        }

        if (config_json.contains("Theme"))
        {
            if (config_json["Theme"].is_number_integer())
            {
                int th = config_json["Theme"].get<int>();
                if (th >= 0)
                {
                    theme_ = th;
                }
                else
                {
                    field_errors += "- Theme: cannot be negative (" + std::to_string(th) + ")\n";
                    has_error = true;
                }
            }
            else
            {
                field_errors += "- Theme: type error using default value\n";
                has_error = true;
            }
        }

        if (has_error)
        {
            error_string_ = "[warning]:\n" + field_errors;
        }
        else
        {
            error_string_.clear();
        }

        return true;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        error_string_ = "[warning] JSON parsing error: " + std::string(e.what());
        return false;
    }
    catch (const std::exception &e)
    {
        error_string_ = "[warning] an error occurred while loading the configuration: " + std::string(e.what());
        return false;
    }
}

bool ConfigLoader::saveConfig(std::filesystem::path path_utf8)
{
    try
    {
        // 目录不存在时自动创建
        if (!path_utf8.parent_path().empty())
        {
            std::filesystem::create_directories(path_utf8.parent_path());
        }

        // 以 raw_json_ 为基底 只覆盖自己认识的字段
        nlohmann::json out_json = raw_json_.is_object()  ? raw_json_  : nlohmann::json::object();

        out_json["Version"] = version_;
        out_json["DefaultLanguage"] = default_language_;

        std::string tag_mode_str;
        switch (tag_mode_)
        {
        case TagFileManager::StoreMode::Sidecar:
            tag_mode_str = "Sidecar";
            break;
        case TagFileManager::StoreMode::Filename:
            tag_mode_str = "Filename";
            break;
        default:
            tag_mode_str = "Sidecar";
            break;
        }

        out_json["TagMode"] = tag_mode_str;
        out_json["ServerWaitingTime"] = server_waiting_time_.count();
        out_json["DownloadPath"] = download_path_.string();
        out_json["BroadcastPort"] = broadcast_port_;
        out_json["BroadcastMagicWord"] = broadcast_magic_word_;
        out_json["FontSize"] = font_size_;
        out_json["Theme"] = theme_;

        std::ofstream file(path_utf8);
        if (file.is_open())
        {
            file << out_json.dump(4) << std::endl;
            error_string_.clear();
            return true;
        }
        else
        {
            error_string_ = "[warning] can't create profile: " + path_utf8.string();
            return false;
        }
    }
    catch (const std::exception &e)
    {
        error_string_ = "[warning] an error occurred while creating the profile: " + std::string(e.what());
        return false;
    }
}