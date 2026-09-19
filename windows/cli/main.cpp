#include <iostream>
#include <nlohmann/json.hpp>

#include "core/include/directory_manager.h"
#include "core/include/tag_serve.h"

#define UDP_DEFAULT_PORT 11451
#define UDP_DEFAULT_MAGIC "0x114514"

#ifdef _WIN32
#include <windows.h>

static std::string ansiToUtf8(const std::string &ansi)
{
    if (ansi.empty())
    {
        return {};
    }

    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), (int)ansi.size(), nullptr, 0);
    if (wlen <= 0)
    {
        return ansi;
    }

    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), (int)ansi.size(), &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (u8len <= 0)
    {
        return ansi;
    }

    std::string u8str(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &u8str[0], u8len, nullptr, nullptr);
    return u8str;
}

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

    // 保留完整 JSON 用于保留 GUI / CLI 各自独有的字段
    nlohmann::json raw_json_ = nlohmann::json::object();

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
                else if (mode == "Embedded" || mode == "Filename")
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
                    field_errors += "  - BroadcastPort: out of range 1-65535 (" + std::to_string(port) + ")\n";
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
    try
    {
        // 如果文件已存在 先读进来保留未知字段
        if (std::filesystem::exists(path_utf8))
        {
            std::ifstream in(path_utf8);
            if (in.is_open())
            {
                try
                {
                    nlohmann::json existing;
                    in >> existing;
                    if (existing.is_object())
                    {
                        raw_json_ = existing;
                    }
                }
                catch (...)
                {
                    // 文件坏了就当没有 用当前 raw_json_
                }
            }
        }

        // 以 raw_json_ 为基底 只覆盖自己认识的字段
        nlohmann::json out_json = raw_json_.is_object() ? raw_json_ : nlohmann::json::object();

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

        // 确保目录存在
        if (!path_utf8.parent_path().empty() &&
            !std::filesystem::exists(path_utf8.parent_path()))
        {
            std::filesystem::create_directories(path_utf8.parent_path());
        }

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
    std::string color = "#FFB6C1";

    command_s.commands_.push_back("help");
    command_s.commands_.push_back("root");
    command_s.commands_.push_back("tag");
    command_s.commands_.push_back("file");
    command_s.commands_.push_back("search");

    std::string input;

    for (int i = 1; i < argc; ++i)
    {
        if (i > 1)
        {
            input += " ";
        }

#ifdef _WIN32
        std::string arg = ansiToUtf8(argv[i]);
#else
        std::string arg = argv[i];
#endif

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
                std::cout << "list:" << std::endl;
                for (auto path : dir_m.getValidDirList())
                {
                    std::cout << path << std::endl;
                }
                std::cout << std::endl;
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
                        std::cout << "added:" << parameter[2] << std::endl;
                    }
                    else
                    {
                        std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                    }
                }
                else
                {
                    std::cout << dir_m.getLastError() << std::endl;
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
                        std::cout << "removed:" << parameter[2] << std::endl;
                    }
                    else
                    {
                        std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                    }
                }
                else
                {
                    std::cout << dir_m.getLastError() << std::endl;
                }
            }
            break;
        default:
            break;
        }

        if (!matched)
        {
            std::cout << "available commands: list / add / remove" << std::endl;
            std::cout << "tagmeow help" << std::endl;
            std::cout << "tagmeow root list" << std::endl;
            std::cout << "tagmeow root add <path>" << std::endl;
            std::cout << "tagmeow root remove <path>" << std::endl;
        }

        break;
    }

    // tag
    case 2:
    {
        bool matched = false;

        if (size < 2)
        {
            break;
        }

        if (parameter[1] == "list" && size == 2)
        {
            matched = true;
            std::cout << "list:" << std::endl;
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
        else if (parameter[1] == "addtype" && size >= 3)
        {
            matched = true;
            if (size >= 4)
            {
                color = parameter[3];
            }

            if (tag_m.addType(parameter[2], color))
            {
                std::cout << "added" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "addtag" && size >= 4)
        {
            matched = true;

            for (size_t i = 3; i < size; i++)
            {
                temp_strs.push_back(parameter[i]);
            }

            if (tag_m.addTag(parameter[2], temp_strs))
            {
                std::cout << "added" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "removetag" && size >= 3)
        {
            matched = true;
            for (size_t i = 2; i < size; i++)
            {
                temp_strs.push_back(parameter[i]);
            }

            if (tag_m.removeTag(temp_strs))
            {
                std::cout << "removed" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "removetype" && size >= 3)
        {
            matched = true;
            for (size_t i = 2; i < size; i++)
            {
                temp_strs.push_back(parameter[i]);
            }

            if (tag_m.removeType(temp_strs))
            {
                std::cout << "removed" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "renametag" && size == 4)
        {
            matched = true;
            if (tag_m.renameTag(parameter[2], parameter[3]))
            {
                std::cout << "renamed" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "renametype" && size == 4)
        {
            matched = true;
            if (tag_m.renameType(parameter[2], parameter[3]))
            {
                std::cout << "renamed" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "resettype" && size == 4)
        {
            matched = true;
            if (tag_m.setTagType(parameter[2], parameter[3]))
            {
                std::cout << "success" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }

        if (!matched)
        {
            std::cout << "available commands: list / save / reload / addtag / addtype / removetag / removetype / renametag / renametype / resettype" << std::endl;
            std::cout << "tagmeow tag list" << std::endl;
            std::cout << "tagmeow tag addtag <type> <tag1,tag2,...>" << std::endl;
            std::cout << "tagmeow tag addtype <type> [color]" << std::endl;
            std::cout << "tagmeow tag removetag <tag1,tag2,...>" << std::endl;
            std::cout << "tagmeow tag removetype <type1,type2,...>" << std::endl;
            std::cout << "tagmeow tag renametag <old> <new>" << std::endl;
            std::cout << "tagmeow tag renametype <old> <new>" << std::endl;
            std::cout << "tagmeow tag resettype <tag> <type>" << std::endl;
        }

        break;
    }

    // file
    case 3:
    {
        bool matched = false;

        if (size < 2)
        {
            break;
        }

        if (parameter[1] == "convertmode" && size == 2)
        {
            matched = true;
            TagFileManager::StoreMode from = config.tag_mode_;
            TagFileManager::StoreMode to = (from == TagFileManager::StoreMode::Sidecar) ? TagFileManager::StoreMode::Filename : TagFileManager::StoreMode::Sidecar;
            if (tag_m.convertMode(from, to))
            {
                config.tag_mode_ = to;
                config.saveConfig();
                std::cout << "success" << std::endl;
            }
            else
            {
                std::cout << tag_m.getLastError() << " / " << tag_m.getFileError() << std::endl;
            }
        }
        else if (parameter[1] == "add" && size >= 4)
        {
            matched = true;
            for (size_t i = 3; i < size; i++)
            {
                temp_strs.push_back(parameter[i]);
            }

            if (tag_m.addFileTag(parameter[2], temp_strs))
            {
                std::cout << "added" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }
        else if (parameter[1] == "remove" && size >= 4)
        {
            matched = true;
            for (size_t i = 3; i < size; i++)
            {
                temp_strs.push_back(parameter[i]);
            }

            if (tag_m.removeFileTag(parameter[2], temp_strs))
            {
                std::cout << "removed" << std::endl;
            }
            else
            {
                std::cout << tag_m.getTagError() << std::endl;
            }
        }

        if (!matched)
        {
            std::cout << "available commands: convertmode / info / add / remove" << std::endl;
            std::cout << "tagmeow file convertmode" << std::endl;
            std::cout << "tagmeow file info <path>" << std::endl;
            std::cout << "tagmeow file add <path> <tag1,tag2,...>" << std::endl;
            std::cout << "tagmeow file remove <path> <tag1,tag2,...>" << std::endl;
        }

        break;
    }

    // search
    case 4:

        if (size > 2)
        {
            FileDatabase::SearchOptions s_tags;
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
                    s_tags.include_.push_back(parameter[i]);
                }
                if (is_exclude)
                {
                    s_tags.exclude_.push_back(parameter[i]);
                }
                if (is_only)
                {
                    s_tags.only_.push_back(parameter[i]);
                }
            }
            std::cout << "file list:" << std::endl;
            for (auto file : tag_m.searchByTags(s_tags))
            {
                std::cout << file.path_ << "\ntags: ";
                for (auto tag : file.tags_)
                {
                    std::cout << tag << ",";
                }
                std::cout << "\n";
            }
        }
        else
        {
            std::cout << "available commands: help / root / tag / file / search" << std::endl;
            std::cout << "tagmeow search [-i <tag1,tag2,...>] [-e <tag1,tag2,...>] [-o <tag1,tag2,...>]" << std::endl;
        }

        break;

    default:
        std::cout << "unknown command, try: tagmeow help" << std::endl;
        break;
    }

    dir_m.saveToFile();
    tag_m.saveTag();

    return 0;
}