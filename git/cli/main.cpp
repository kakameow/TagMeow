#include <iostream>
#include <nlohmann/json.hpp>

#include "core/include/directory_manager.h"
#include "core/include/tag_serve.h"

#define UDP_DEFAULT_PORT 11451
#define UDP_DEFAULT_MAGIC "0x114514"

#ifdef _WIN32
#include <windows.h>
#endif

struct ConfigLoader
{
    ConfigLoader();
    ~ConfigLoader();

    // 加载配置覆盖默认值 失败的值使用默认值
    bool loadConfig(std::filesystem::path path_utf8 = "./config/config.json");
    bool saveConfig(std::filesystem::path path_utf8 = "./config/config.json");

    std::string version_ = "beta";
    std::string default_language_ = "zh-cn";
    TagFileManager::StoreMode tag_mode_ = TagFileManager::StoreMode::Sidecar;
    std::chrono::minutes server_waiting_time_ = std::chrono::minutes(5);
    std::filesystem::path download_path_ = "./download";
    std::uint16_t broadcast_port_ = UDP_DEFAULT_PORT;
    std::string broadcast_magic_word_ = UDP_DEFAULT_MAGIC;

    mutable std::string error_string_;
};

ConfigLoader::ConfigLoader()
{
    if (!loadConfig())
    {
        std::filesystem::path config_path = "./config/config.json";
        std::filesystem::create_directories(config_path.parent_path());
        saveConfig();
    }
}

struct InputParser
{
    InputParser();
    InputParser(const std::vector<std::string> &commands);
    ~InputParser();

    // 将输入字符串按空格 逗号分隔 引号包裹算一个整体 返回 命令在 commands_ 的索引 未找到返回 std::string::npos
    size_t parseCommand(const std::string &input, std::vector<std::string> &parameters);

    std::vector<std::string> commands_;
};

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
                else if (mode == "Embedded")
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
                    field_errors += "  - BroadcastPort: out of range 1-65535 (" + std::to_string(port) + ")" + std::to_string(UDP_DEFAULT_PORT) + "\n";
                    has_error = true;
                }
            }
            else
            {
                field_errors += "  - BroadcastPort: type error using default value\n";
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
                field_errors += "  - BroadcastMagicWord: type error using default value\n";
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
    if (!std::filesystem::exists(path_utf8))
    {
        error_string_ = "[warning] file does not exist: " + path_utf8.string();
        return false;
    }

    try
    {
        nlohmann::json default_config;
        default_config["Version"] = version_;
        default_config["DefaultLanguage"] = default_language_;

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

        default_config["TagMode"] = tag_mode_str;
        default_config["ServerWaitingTime"] = server_waiting_time_.count();
        default_config["DownloadPath"] = download_path_.string();
        default_config["BroadcastPort"] = broadcast_port_;
        default_config["BroadcastMagicWord"] = broadcast_magic_word_;

        std::ofstream file(path_utf8);
        if (file.is_open())
        {
            file << default_config.dump(4) << std::endl;
            error_string_ = "[tip] the default configuration file has been created: " + path_utf8.string();
            return false;
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
    return true;
}

InputParser::InputParser()
{
}

InputParser::~InputParser()
{
}

InputParser::InputParser(const std::vector<std::string> &commands) : commands_(commands)
{
}

size_t InputParser::parseCommand(const std::string &input, std::vector<std::string> &parameters)
{
    parameters.clear();

    if (input.empty())
    {
        return std::string::npos;
    }

    std::vector<std::string> tokens;
    std::string current_token;
    bool in_quotes = false;

    for (size_t i = 0; i < input.length(); i++)
    {
        char ch = input[i];

        if (ch == '"')
        {
            in_quotes = !in_quotes;
            continue;
        }

        if ((ch == ' ' || ch == ',') && !in_quotes)
        {
            if (!current_token.empty())
            {
                tokens.push_back(current_token);
                current_token.clear();
            }
        }
        else
        {
            current_token += ch;
        }
    }

    if (!current_token.empty())
    {
        tokens.push_back(current_token);
    }

    if (tokens.empty())
    {
        return std::string::npos;
    }

    for (size_t i = 0; i < commands_.size(); i++)
    {
        if (commands_[i] == tokens[0])
        {
            parameters = std::move(tokens);
            return i;
        }
    }

    return std::string::npos;
}

int main(int argc, char const *argv[])
{

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
    {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
#endif

    ConfigLoader config;
    InputParser command_s;
    DirectoryConfigManager dir_m("./config/path.json");
    TagServe tag_m(dir_m.getValidDirList(), config.tag_mode_, "./config/tag.json", "./config/index.db");

    command_s.commands_.push_back("help");
    command_s.commands_.push_back("root");
    command_s.commands_.push_back("tag");
    command_s.commands_.push_back("file");
    command_s.commands_.push_back("search");

    std::string input;

    for (size_t i = 1; i < argc; i++)
    {
        if (i > 1)
        {
            input += " ";
        }

        std::string arg = argv[i];

        if (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos)
        {
            input += "\"" + arg + "\"";
        }
        else
        {
            input += arg;
        }
    }

    if (input.empty())
    {
        std::cout << "tagmeow <command> [parameters...]" << std::endl;
        std::cout << "available commands: help / root / tag / file / search" << std::endl;
        return 0;
    }

    std::vector<std::string> parameter;
    std::vector<std::string> temp_strs;
    size_t index = command_s.parseCommand(input, parameter);
    size_t size = parameter.size();

    switch (index)
    {
        
    // help
    case 0:
        std::cout << "available commands: help / root / tag / file / search" << std::endl;
        std::cout << "tagmeow help" << std::endl;
        std::cout << "tagmeow root list" << std::endl;
        std::cout << "tagmeow root add <path>" << std::endl;
        std::cout << "tagmeow root remove <path>" << std::endl;
        std::cout << "tagmeow tag list" << std::endl;
        std::cout << "tagmeow tag addtag <type> <tag1,tag2,...>" << std::endl;
        std::cout << "tagmeow tag addtype <type> [color]" << std::endl;
        std::cout << "tagmeow tag removetag <tag1,tag2,...>" << std::endl;
        std::cout << "tagmeow tag removetype <type1,type2,...>" << std::endl;
        std::cout << "tagmeow tag renametag <old> <new>" << std::endl;
        std::cout << "tagmeow tag renametype <old> <new>" << std::endl;
        std::cout << "tagmeow tag resettype <tag> <type>" << std::endl;
        std::cout << "tagmeow file convertmode" << std::endl;
        std::cout << "tagmeow file add <path> <tag1,tag2,...>" << std::endl;
        std::cout << "tagmeow file remove <path> <tag1,tag2,...>" << std::endl;
        std::cout << "tagmeow search [-i <tag1,tag2,...>] [-e <tag1,tag2,...>] [-o <tag1,tag2,...>]" << std::endl;
        break;

    // root
    case 1:
    {
        bool matched = false;

        switch (size)
        {
        case 2:
            if (parameter[1] == "list")
            {
                matched = true;
                for (auto path : dir_m.getValidDirList())
                {
                    std::cout << path << std::endl;
                }
            }
            else if (parameter[1] == "reload")
            {
                matched = true;
                dir_m.clearInvalidPath();
                if (tag_m.reLoadRoot(dir_m.getValidDirList()))
                {
                    std::cout << "root reload done" << std::endl;
                }
                else
                {
                    std::cout << "root reload failed" << std::endl;
                }
            }
            break;

        case 3:
            if (parameter[1] == "add")
            {
                matched = true;
                if (dir_m.addDirectory(parameter[2]))
                {
                    if (tag_m.addRoot(dir_m.getLastValidDir()))
                    {
                        dir_m.saveToFile();
                        std::cout << "root added: " << parameter[2] << std::endl;
                    }
                    else
                    {
                        std::cout << "add root failed" << std::endl;
                    }
                }
                else
                {
                    std::cout << "invalid path: " << parameter[2] << std::endl;
                }
            }
            else if (parameter[1] == "remove")
            {
                matched = true;
                if (dir_m.removeDirectory(parameter[2]))
                {
                    if (tag_m.removeRoot(parameter[2]))
                    {
                        dir_m.saveToFile();
                        std::cout << "root removed: " << parameter[2] << std::endl;
                    }
                    else
                    {
                        std::cout << "remove root failed" << std::endl;
                    }
                }
                else
                {
                    std::cout << "path not found: " << parameter[2] << std::endl;
                }
            }
            break;

        default:
            break;
        }

        if (!matched)
        {
            std::cout << "usage: tagmeow root list / reload / add <path> / remove <path>" << std::endl;
        }
        break;
    }

    // tag
    case 2:
    {
        bool matched = false;

        switch (size)
        {
        case 2:
            if (parameter[1] == "list")
            {
                matched = true;
                for (const auto &pair : tag_m.getTypeTag())
                {
                    std::cout << "Type: " << pair.first << std::endl;
                    std::cout << "Values: ";
                    for (const auto &value : pair.second)
                    {
                        std::cout << value << " ";
                    }
                    std::cout << std::endl;
                }
            }
            else if (parameter[1] == "save")
            {
                matched = true;
                if (tag_m.saveTag())
                {
                    std::cout << "tag saved" << std::endl;
                }
                else
                {
                    std::cout << "save tag failed" << std::endl;
                }
            }
            else if (parameter[1] == "reload")
            {
                matched = true;
                if (tag_m.reLoadTag("./config/tag.json"))
                {
                    std::cout << "tag reloaded" << std::endl;
                }
                else
                {
                    std::cout << "reload tag failed" << std::endl;
                }
            }
            break;

        default:
            if (size > 3)
            {
                for (size_t i = 2; i < size; i++)
                {
                    temp_strs.push_back(parameter[i]);
                }

                if (parameter[1] == "addtag")
                {
                    matched = true;
                    std::swap(temp_strs[2], temp_strs.back());
                    temp_strs.pop_back();

                    if (tag_m.addTag(parameter[2], temp_strs))
                    {
                        std::cout << "tag added" << std::endl;
                    }
                    else
                    {
                        std::cout << "add tag failed" << std::endl;
                    }
                }
                else if (parameter[1] == "addtype")
                {
                    matched = true;
                    if (tag_m.addType(parameter[2], parameter[3]))
                    {
                        std::cout << "type added" << std::endl;
                    }
                    else
                    {
                        std::cout << "add type failed" << std::endl;
                    }
                }
                else if (parameter[1] == "removetag")
                {
                    matched = true;
                    if (tag_m.removeTag(temp_strs))
                    {
                        std::cout << "tag removed" << std::endl;
                    }
                    else
                    {
                        std::cout << "remove tag failed" << std::endl;
                    }
                }
                else if (parameter[1] == "removetype")
                {
                    matched = true;
                    if (tag_m.removeType(temp_strs))
                    {
                        std::cout << "type removed" << std::endl;
                    }
                    else
                    {
                        std::cout << "remove type failed" << std::endl;
                    }
                }
                else if (parameter[1] == "renametag")
                {
                    matched = true;
                    if (tag_m.renameTag(parameter[2], parameter[3]))
                    {
                        std::cout << "tag renamed" << std::endl;
                    }
                    else
                    {
                        std::cout << "rename tag failed" << std::endl;
                    }
                }
                else if (parameter[1] == "renametype")
                {
                    matched = true;
                    if (tag_m.renameType(parameter[2], parameter[3]))
                    {
                        std::cout << "type renamed" << std::endl;
                    }
                    else
                    {
                        std::cout << "rename type failed" << std::endl;
                    }
                }
                else if (parameter[1] == "resetcolor")
                {
                    matched = true;
                    if (tag_m.setTypeColor(parameter[2], parameter[3]))
                    {
                        std::cout << "type color updated" << std::endl;
                    }
                    else
                    {
                        std::cout << "reset color failed" << std::endl;
                    }
                }
                else if (parameter[1] == "resettype")
                {
                    matched = true;
                    if (tag_m.setTagType(parameter[2], parameter[3]))
                    {
                        std::cout << "tag type updated" << std::endl;
                    }
                    else
                    {
                        std::cout << "reset type failed" << std::endl;
                    }
                }
            }
            break;
        }

        if (!matched)
        {
            std::cout << "usage: tagmeow tag list / save / reload / addtype <type> <#RRGGBB> / addtag <type> <tags...> / removetag <tags...> / removetype <types...> / renametag <old> <new> / renametype <old> <new> / resetcolor <type> <#RRGGBB> / resettype <tag> <type>" << std::endl;
        }
        break;
    }

    // file
    case 3:
    {
        bool matched = false;

        switch (size)
        {
        case 2:
            if (parameter[1] == "convertmode")
            {
                matched = true;
                TagFileManager::StoreMode from = config.tag_mode_;
                TagFileManager::StoreMode to = (from == TagFileManager::StoreMode::Sidecar) ? TagFileManager::StoreMode::Filename : TagFileManager::StoreMode::Sidecar;
                if (tag_m.convertMode(from, to))
                {
                    config.tag_mode_ = to;
                    config.saveConfig();
                    std::cout << "mode converted" << std::endl;
                }
                else
                {
                    std::cout << "convert mode failed" << std::endl;
                }
            }
            break;

        case 3:
            if (parameter[1] == "info")
            {
                matched = true;
                auto info = tag_m.getFileInfo(parameter[2]);
                if (info.has_value())
                {
                    auto &f = info.value();
                    std::cout << "file_id_: " << f.file_id_ << std::endl;
                    std::cout << "path_: " << f.path_ << std::endl;
                    std::cout << "rel_path_: " << f.rel_path_ << std::endl;
                    std::cout << "file_mtime_: " << f.file_mtime_ << std::endl;
                    std::cout << "file_size_: " << f.file_size_ << std::endl;
                    std::cout << "sidecar_mtime_: " << f.sidecar_mtime_ << std::endl;
                    std::cout << "tags: ";
                    for (auto t : f.tags_)
                    {
                        std::cout << t << " ";
                    }
                    std::cout << std::endl;
                    std::cout << "file_version_: " << f.file_version_ << std::endl;
                    std::cout << "last_refresh_time_: " << f.last_refresh_time_ << std::endl;
                }
                else
                {
                    std::cout << "file not found" << std::endl;
                }
            }
            break;

        default:
            if (size > 3)
            {
                for (size_t i = 3; i < size; i++)
                {
                    temp_strs.push_back(parameter[i]);
                }

                if (parameter[1] == "add")
                {
                    matched = true;
                    if (tag_m.addFileTag(parameter[2], temp_strs))
                    {
                        std::cout << "file tag added" << std::endl;
                        if (tag_m.updateFile(parameter[2]))
                        {
                            std::cout << "db updated" << std::endl;
                        }
                    }
                    else
                    {
                        std::cout << "add file tag failed" << std::endl;
                    }
                }
                else if (parameter[1] == "remove")
                {
                    matched = true;
                    if (tag_m.removeFileTag(parameter[2], temp_strs))
                    {
                        std::cout << "file tag removed" << std::endl;
                        if (tag_m.updateFile(parameter[2]))
                        {
                            std::cout << "db updated" << std::endl;
                        }
                    }
                    else
                    {
                        std::cout << "remove file tag failed" << std::endl;
                    }
                }
            }
            break;
        }

        if (!matched)
        {
            std::cout << "usage: tagmeow file convertmode / info <path> / add <path> <tags...> / remove <path> <tags...>" << std::endl;
        }
        break;
    }

    // search
    case 4:
    {
        if (size > 2)
        {
            FileDatabase::SearchOptions opts;
            bool is_include = false;
            bool is_exclude = false;
            bool is_only = false;

            for (size_t i = 1; i < size; i++)
            {
                if (parameter[i] == "-i")
                {
                    is_include = true;
                    is_exclude = false;
                    is_only = false;
                    continue;
                }
                if (parameter[i] == "-e")
                {
                    is_include = false;
                    is_exclude = true;
                    is_only = false;
                    continue;
                }
                if (parameter[i] == "-o")
                {
                    is_include = false;
                    is_exclude = false;
                    is_only = true;
                    continue;
                }

                if (is_include)
                {
                    opts.include_.push_back(parameter[i]);
                }
                if (is_exclude)
                {
                    opts.exclude_.push_back(parameter[i]);
                }
                if (is_only)
                {
                    opts.only_.push_back(parameter[i]);
                }
                    
            }

            for (auto file : tag_m.searchByTags(opts))
            {
                std::cout << file.path_ << "\ntags: ";
                for (auto t : file.tags_)
                {
                    std::cout << t << ",";
                }
                std::cout << "\n";
            }
        }
        else
        {
            std::cout << "usage: tagmeow search [-i <tags...>] [-e <tags...>] [-o <tags...>]" << std::endl;
        }
        break;
    }

    default:
        std::cout << "unknown command, try: tagmeow help" << std::endl;
        break;
    }

    dir_m.saveToFile();
    tag_m.saveTag();

    return 0;
}