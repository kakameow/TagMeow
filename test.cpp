#include <iostream>
#include <nlohmann/json.hpp>

#include "module_1/directory_manager.h"
#include "module_1/language_manager.h"
#include "module_2/tag_serve.h"
#include "module_3/sync_server.h"
#include "module_3/sync_client.h"

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

auto getCurrentTimestamp = []() -> std::string
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %1000;
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();

    return ss.str();
};

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
    LanguageManager language_m("./language");
    TagServe tag_m(dir_m.getValidDirList(), config.tag_mode_, "./config/tag.json", "./config/index.db");
    SyncServer s_server(config.broadcast_port_, config.broadcast_magic_word_, config.server_waiting_time_);
    SyncClient s_client(config.broadcast_port_, config.broadcast_magic_word_, config.download_path_);

    language_m.loadLanguage(config.default_language_);

    command_s.commands_.push_back("help");
    command_s.commands_.push_back("exit");

    command_s.commands_.push_back("language");
    command_s.commands_.push_back("root");
    command_s.commands_.push_back("tag");
    command_s.commands_.push_back("file");
    command_s.commands_.push_back("search");
    command_s.commands_.push_back("server");
    command_s.commands_.push_back("client");

    bool running = true;
    size_t index = 0;
    size_t size = 0;
    std::string input;
    std::vector<std::string> parameter;
    std::vector<std::string> temp_strs;

    std::ofstream log_file;
    std::filesystem::path log_dir = "./config";

    if (!std::filesystem::exists(log_dir))
    {
        std::filesystem::create_directories(log_dir);
    }

    log_file.open("./config/log.txt", std::ios::app);
    if (!log_file.is_open())
    {
        std::cerr << "Warning: Failed to open log file" << std::endl;
    }

    while (running)
    {
        std::cout << "tagmeow>";
        std::getline(std::cin, input);

        if (input.empty() || input.find_first_not_of(" \t") == std::string::npos)
        {
            continue;
        }

        index = command_s.parseCommand(input, parameter);
        size = parameter.size();
        temp_strs.clear();

        switch (index)
        {
        case 0:
            std::cout << language_m.getString("help.all") << std::endl;
            break;
        case 1:
        {
            dir_m.saveToFile();
            tag_m.saveTag();

            std::error_code ec;
            s_server.stop(ec);
            s_client.disconnect();

            running = false;
            break;
        }
        case 2:
        {
            bool matched = false;

            switch (size)
            {
            case 2:
                if (parameter[1] == "list")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.language.list") << std::endl;
                    temp_strs = language_m.getLanguagesList();
                    for (auto str : temp_strs)
                    {
                        std::cout << str << std::endl;
                    }
                    std::cout << std::endl;
                }
                else if (parameter[1] == "reload")
                {
                    matched = true;
                    if (language_m.loadLanguageList("./language"))
                    {
                        std::cout << language_m.getString("tip.language.reload") << std::endl;
                    }
                    else
                    {
                        //std::cout << language_m.getLastError() << std::endl;
                        std::cout << language_m.getString("help.language.reload") << std::endl;
                    }
                }
                break;
            case 3:
                if (parameter[1] == "change")
                {
                    matched = true;
                    if (language_m.loadLanguage(parameter[2]))
                    {
                        std::cout << language_m.getString("tip.language.change") << std::endl;
                    }
                    else
                    {
                        //std::cout << language_m.getLastError() << std::endl;
                        std::cout << language_m.getString("help.language.change") << std::endl;
                    }
                }
                break;
            default:
                break;
            }
            if (!matched)
            {
                std::cout << language_m.getString("help.language.all") << std::endl;
            }

            break;
        }
        case 3:
        {
            bool matched = false;

            switch (size)
            {
            case 2:
                if (parameter[1] == "reload")
                {
                    matched = true;
                    dir_m.clearInvalidPath();
                    if (tag_m.reLoadRoot(dir_m.getValidDirList()))
                    {
                        std::cout << language_m.getString("tip.root.reload") << std::endl;
                    }
                    else
                    {
                        //std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                        std::cout << language_m.getString("help.root.reload") << std::endl;
                    }
                }
                else if (parameter[1] == "list")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.root.list") << std::endl;
                    for (auto path : dir_m.getValidDirList())
                    {
                        std::cout << path << std::endl;
                    }
                    std::cout << std::endl;
                }
                break;
            case 3:
                if (parameter[1] == "reload")
                {
                    matched = true;
                    if (tag_m.reLoadRoot(parameter[2]))
                    {
                        std::cout << language_m.getString("tip.root.reload") << parameter[2] << std::endl;
                    }
                    else
                    {
                        //std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                        std::cout << language_m.getString("help.root.reload") << std::endl;
                    }
                }
                else if (parameter[1] == "add")
                {
                    matched = true;
                    if (dir_m.addDirectory(parameter[2]))
                    {
                        if (tag_m.addRoot(dir_m.getLastValidDir()))
                        {
                            dir_m.saveToFile();
                            std::cout << language_m.getString("tip.root.add") << parameter[2] << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                            std::cout << language_m.getString("help.root.add") << std::endl;
                        }
                    }
                    else
                    {
                        //std::cout << dir_m.getLastError() << std::endl;
                        std::cout << language_m.getString("help.root.add") << std::endl;
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
                            std::cout << language_m.getString("tip.root.remove") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getLastError() << " / " << tag_m.getDBError() << std::endl;
                            std::cout << language_m.getString("help.root.remove") << std::endl;
                        }
                    }
                    else
                    {
                        //std::cout << dir_m.getLastError() << std::endl;
                        std::cout << language_m.getString("help.root.remove") << std::endl;
                    }
                }
                break;
            default:
                break;
            }
            if (!matched)
            {
                std::cout << language_m.getString("help.root.all") << std::endl;
            }

            break;
        }
        case 4:
        {
            bool matched = false;

            switch (size)
            {
            case 2:
                if (parameter[1] == "reload")
                {
                    matched = true;
                    if (tag_m.reLoadTag("./config/tag.json"))
                    {
                        std::cout << language_m.getString("tip.tag.reload") << std::endl;
                    }
                    else
                    {
                        //std::cout << tag_m.getTagError() << std::endl;
                        std::cout << language_m.getString("help.tag.reload") << std::endl;
                    }
                }
                else if (parameter[1] == "save")
                {
                    matched = true;
                    if (tag_m.saveTag())
                    {
                        std::cout << language_m.getString("tip.tag.save") << std::endl;
                    }
                    else
                    {
                        //std::cout << tag_m.getTagError() << std::endl;
                        std::cout << language_m.getString("help.tag.save") << std::endl;
                    }
                }
                else if (parameter[1] == "list")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.tag.list") << std::endl;
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
                            std::cout << language_m.getString("tip.tag.addtag") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.addtag") << std::endl;
                        }
                    }
                    else if (parameter[1] == "addtype")
                    {
                        matched = true;
                        if (tag_m.addType(parameter[2], parameter[3]))
                        {
                            std::cout << language_m.getString("tip.tag.addtype") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.addtype") << std::endl;
                        }
                    }
                    else if (parameter[1] == "removetag")
                    {
                        matched = true;
                        if (tag_m.removeTag(temp_strs))
                        {
                            std::cout << language_m.getString("tip.tag.removetag") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.removetag") << std::endl;
                        }
                    }
                    else if (parameter[1] == "removetype")
                    {
                        matched = true;
                        if (tag_m.removeType(temp_strs))
                        {
                            std::cout << language_m.getString("tip.tag.removetype") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.removetype") << std::endl;
                        }
                    }
                    else if (parameter[1] == "renametag")
                    {
                        matched = true;
                        if (tag_m.renameTag(parameter[2], parameter[3]))
                        {
                            std::cout << language_m.getString("tip.tag.renametag") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.renametag") << std::endl;
                        }
                    }
                    else if (parameter[1] == "renametype")
                    {
                        matched = true;
                        if (tag_m.renameType(parameter[2], parameter[3]))
                        {
                            std::cout << language_m.getString("tip.tag.renametype") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.renametype") << std::endl;
                        }
                    }
                    else if (parameter[1] == "resetcolor")
                    {
                        matched = true;
                        if (tag_m.setTypeColor(parameter[2], parameter[3]))
                        {
                            std::cout << language_m.getString("tip.tag.resetcolor") << std::endl;
                        }
                        else
                        {
                            //::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.resetcolor") << std::endl;
                        }
                    }
                    else if (parameter[1] == "resettype")
                    {
                        matched = true;
                        if (tag_m.setTagType(parameter[2], parameter[3]))
                        {
                            std::cout << language_m.getString("tip.tag.resettype") << std::endl;
                        }
                        else
                        {
                            //std::cout << tag_m.getTagError() << std::endl;
                            std::cout << language_m.getString("help.tag.resettype") << std::endl;
                        }
                    }
                }
                break;
            }
            if (!matched)
            {
                std::cout << language_m.getString("help.tag.all") << std::endl;
            }

            break;
        }
        case 5:
        {
            bool matched = false;

            switch (size)
            {
            case 2:

                if (parameter[1] == "convertmode")
                {
                    matched = true;
                    TagFileManager::StoreMode form = config.tag_mode_;
                    TagFileManager::StoreMode to;
                    if (form == TagFileManager::StoreMode::Sidecar)
                    {
                        to = TagFileManager::StoreMode::Filename;
                    }
                    else
                    {
                        to = TagFileManager::StoreMode::Sidecar;
                    }
                    if (tag_m.convertMode(form, to))
                    {
                        config.tag_mode_ = to;
                        config.saveConfig();
                        std::cout << language_m.getString("tip.file.convertmode") << std::endl;
                    }
                    else
                    {
                        //std::cout << tag_m.getLastError() << " / " << tag_m.getFileError() << std::endl;
                        std::cout << language_m.getString("help.file.convertmode") << std::endl;
                    }
                }

                break;
                case 3:
                    if (parameter[1] == "info")
                    {
                        matched = true;
                        auto info = tag_m.getFileInfo(parameter[2]);
                        std::cout << language_m.getString("tip.file.info") << std::endl;
                        if (info.has_value())
                        {
                            auto &file_info = info.value();
                            std::cout << "file_id_: " << file_info.file_id_ << std::endl;
                            std::cout << "path_: " << file_info.path_ << std::endl;
                            std::cout << "rel_path_: " << file_info.rel_path_ << std::endl;
                            std::cout << "file_mtime_: " << file_info.file_mtime_ << std::endl;
                            std::cout << "file_size_: " << file_info.file_size_ << std::endl;
                            std::cout << "sidecar_mtime_: " << file_info.sidecar_mtime_ << std::endl;

                            std::cout << "tags: ";
                            for (auto tag : file_info.tags_)
                            {
                                std::cout << tag << " ";
                            }
                            std::cout << "\n";

                            std::cout << "file_version_: " << file_info.file_version_ << std::endl;
                            std::cout << "last_refresh_time_: " << file_info.last_refresh_time_ << std::endl;
                        }
                        else
                        {
                            std::cout << language_m.getString("help.file.info") << std::endl;
                        }
                    }
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
                                std::cout << language_m.getString("tip.file.add") << std::endl;
                                if (tag_m.updateFile(parameter[2]))
                                {
                                    std::cout << language_m.getString("tip.file.db") << std::endl;
                                }
                                else
                                {
                                    //std::cout << tag_m.getDBError() << std::endl;
                                }
                            }
                            else
                            {
                                //std::cout << tag_m.getTagError() << std::endl;
                                std::cout << language_m.getString("help.file.add") << std::endl;
                            }
                        }
                        else if (parameter[1] == "remove")
                        {
                            matched = true;
                            if (tag_m.removeFileTag(parameter[2], temp_strs))
                            {
                                std::cout << language_m.getString("tip.file.remove") << std::endl;
                                if (tag_m.updateFile(parameter[2]))
                                {
                                    std::cout << language_m.getString("tip.file.db") << std::endl;
                                }
                                else
                                {
                                   //std::cout << tag_m.getDBError() << std::endl;
                                }
                            }
                            else
                            {
                                //std::cout << tag_m.getTagError() << std::endl;
                                std::cout << language_m.getString("help.file.remove") << std::endl;
                            }
                        }
                    }
                    break;
                }
            if (!matched)
            {
                std::cout << language_m.getString("help.file.all") << std::endl;
            }

            break;
        }
        case 6:

            if (size > 2)
            {
                FileDatabase::SearchOptions s_tags;
                bool is_include = false;
                bool is_exclude = false;
                bool is_only = false;

                for (size_t i = 1; i < size; i++)
                {
                    if (parameter[i] == "--include")
                    {
                        is_include = true;
                        is_exclude = false;
                        is_only = false;
                        continue;
                    }

                    if (parameter[i] == "--exclude")
                    {
                        is_include = false;
                        is_exclude = true;
                        is_only = false;
                        continue;
                    }

                    if (parameter[i] == "--only")
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
                std::cout << language_m.getString("tip.search") << std::endl;
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
                std::cout << language_m.getString("help.search.all") << std::endl;
            }

            break;
        case 7:
        {
            bool matched = false;
            std::string empty_msg = language_m.getString("tip.server.queue_empty");
            std::string end_msg = language_m.getString("tip.server.session_end");
            std::function<void(bool, std::error_code)> server_cb = [empty_msg, end_msg, &s_server](bool success, std::error_code e)
            {
                if (success)
                {
                    if (e == std::make_error_code(std::errc::no_message_available))
                    {
                        std::cout << empty_msg << std::endl;
                    }
                    else
                    {
                        std::cout << end_msg << std::endl;
                    }
                }
                else
                {
                    //std::cout << s_server.getLastError() << std::endl;
                }
            };

            switch (size)
            {
            case 2:
                if (parameter[1] == "start")
                {
                    matched = true;
                    std::error_code ec;
                    if (s_server.start("tagmeow", 0, ec, server_cb))
                    {
                        std::cout << language_m.getString("tip.server.start") << std::endl;
                    }
                    else
                    {
                        //std::cout << s_server.getLastError() << std::endl;
                        std::cout << language_m.getString("help.server.start") << std::endl;
                    }
                }
                else if (parameter[1] == "stop")
                {
                    matched = true;
                    std::error_code ec;
                    s_server.stop(ec);
                    if (!ec)
                    {
                        std::cout << language_m.getString("tip.server.stop") << std::endl;
                    }
                    else
                    {
                        //std::cout << s_server.getLastError() << std::endl;
                    }
                }
                else if (parameter[1] == "list")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.server.list") << std::endl;
                    for (auto path : s_server.getTaskQueue())
                    {
                        std::cout << path << std::endl;
                    }
                    std::cout << std::endl;
                }
                else if (parameter[1] == "disconnect")
                {
                    matched = true;
                    std::error_code ec;
                    s_server.disconnect(ec);
                    if (!ec)
                    {
                        std::cout << language_m.getString("tip.server.disconnect") << std::endl;
                    }
                    else
                    {
                        //std::cout << s_server.getLastError() << std::endl;
                    }
                }
                break;
            case 3:
                if (parameter[1] == "start")
                {
                    matched = true;
                    std::error_code ec;
                    if (s_server.start(parameter[2], 0, ec, server_cb))
                    {
                        std::cout << language_m.getString("tip.server.start") << parameter[2] << std::endl;
                    }
                    else
                    {
                        //std::cout << s_server.getLastError() << std::endl;
                        std::cout << language_m.getString("help.server.start") << std::endl;
                    }
                }
                else if (parameter[1] == "add")
                {
                    matched = true;
                    s_server.enqueueDirectory(parameter[2]);
                    std::cout << language_m.getString("tip.server.add") << parameter[2] << std::endl;
                }
                break;
            default:
                break;
            }
            if (!matched)
            {
                std::cout << language_m.getString("help.server.all") << std::endl;
            }
        }
        break;
        case 8:
        {
            bool matched = false;
            std::string empty_msg = language_m.getString("tip.client.empty");
            std::string done_msg = language_m.getString("tip.client.done");
            std::function<void(bool, std::error_code)> client_cb = [empty_msg, done_msg, &s_client](bool success, std::error_code e)
            {
                if (success)
                {
                    if (e == std::make_error_code(std::errc::no_message_available))
                    {
                        std::cout << empty_msg << std::endl;
                    }
                    else
                    {
                        std::cout << done_msg << std::endl;
                    }
                }
                else
                {
                    //std::cout << s_client.getLastError() << std::endl;
                }
            };

            switch (size)
            {
            case 2:
                if (parameter[1] == "scan")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.client.scan") << std::endl;
                    std::vector<ServerInfo> servers = s_client.scanServers();
                    size_t i = 0;
                    for (const auto &server : servers)
                    {
                        std::cout << "[" << i << "] " << server.name_ << " " << server.ip_ << ":" << server.port_ << std::endl;
                        i++;
                    }
                }
                else if (parameter[1] == "disconnect")
                {
                    matched = true;
                    s_client.disconnect();
                    std::cout << language_m.getString("tip.client.disconnect") << std::endl;
                }
                else if (parameter[1] == "clear")
                {
                    matched = true;
                    s_client.clearDownloadRecords();
                    std::cout << language_m.getString("tip.client.clear") << std::endl;
                }
                else if (parameter[1] == "list")
                {
                    matched = true;
                    std::cout << language_m.getString("tip.client.list") << std::endl;
                    size_t i = 0;
                    for (const auto &server : s_client.getServers())
                    {
                        std::cout << "[" << i << "] " << server.name_ << " " << server.ip_ << ":" << server.port_ << std::endl;
                        i++;
                    }
                    std::cout << std::endl;
                }
                break;
            case 3:
                if (parameter[1] == "download")
                {
                    matched = true;
                    size_t server_index = 0;
                    try
                    {
                        server_index = std::stoul(parameter[2]);
                    }
                    catch (const std::exception &)
                    {
                        break;
                    }
                    s_client.startDownload(server_index, client_cb);
                    std::cout << language_m.getString("tip.client.download") << std::endl;
                }
                break;
            default:
                break;
            }
            if (!matched)
            {
                std::cout << language_m.getString("help.client.all") << std::endl;
            }
        }
        break;
        default:
            std::cout << language_m.getString("help.tip") << std::endl;
            break;
        }

        if (!dir_m.getLastError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [DirectoryConfigManager] " << dir_m.getLastError() << std::endl;
        }
        if (!language_m.getLastError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [LanguageManager] " << language_m.getLastError() << std::endl;
        }
        if (!tag_m.getLastError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [TagServe] " << tag_m.getLastError() << std::endl;
        }
        if (!tag_m.getDBError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [TagServe-DB] " << tag_m.getDBError() << std::endl;
        }
        if (!tag_m.getFileError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [TagServe-File] " << tag_m.getFileError() << std::endl;
        }
        if (!tag_m.getTagError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [TagServe-Tag] " << tag_m.getTagError() << std::endl;
        }
        if (!s_client.getLastError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [SyncClient] " << s_client.getLastError() << std::endl;
        }
        if (!s_server.getLastError().empty() && log_file.is_open())
        {
            log_file << "[" << getCurrentTimestamp() << "] [SyncServer] " << s_server.getLastError() << std::endl;
        }

        if (log_file.is_open())
        {
            log_file.flush();
        }
    };

    if (log_file.is_open())
    {
        log_file.close();
    }
    return 0;
}