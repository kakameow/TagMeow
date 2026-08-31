#include "language_manager.h"

LanguageManager::LanguageManager(const std::filesystem::path &language_directory_utf8)
{
    loadLanguageList(language_directory_utf8);
}

LanguageManager::~LanguageManager()
{
}

bool LanguageManager::loadLanguage(const std::string &language_name_utf8)
{
    std::filesystem::path language_path = language_directory_path_ / (language_name_utf8 + ".json");

    std::ifstream file(language_path, std::ios::binary);
    if (!file.is_open())
    {
        error_string_ = "[warning] Cannot open language file: " + language_path.u8string();
        return false;
    }

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        error_string_ = "[warning] JSON parse error in " + language_path.u8string() + ": " + e.what();
        return false;
    }

    if (!config.contains("text") || !config["text"].is_array())
    {
        error_string_ = "[warning] Missing 'text' array in language file";
        return false;
    }

    language_dictionary_.clear();
    if (config.contains("name") && config["name"].is_string())
    {
        language_name_ = config["name"].get<std::string>();
    }
    else
    {
        language_name_ = language_name_utf8;
    }

    for (const auto &item : config["text"])
    {
        std::string id = item["id"].get<std::string>();
        std::string text = item["str"].get<std::string>();
        language_dictionary_[id] = text;
    }

    error_string_.clear();
    return true;
}

bool LanguageManager::loadLanguageList(const std::filesystem::path &directory_path_utf8)
{
    std::error_code ec;
    if (!std::filesystem::exists(directory_path_utf8, ec) || ec)
    {
        error_string_ = "[warning] Directory does not exist: " + directory_path_utf8.u8string();
        return false;
    }

    if (!std::filesystem::is_directory(directory_path_utf8, ec) || ec)
    {
        error_string_ = "[warning] Path is not a directory: " + directory_path_utf8.u8string();
        return false;
    }

    language_list_.clear();
    language_directory_path_ = directory_path_utf8;

    for (const auto &entry : std::filesystem::directory_iterator(directory_path_utf8, ec))
    {
        if (ec)
        {
            error_string_ = "[warning] Error iterating directory: " + ec.message();
            return false;
        }

        if (!entry.is_regular_file(ec))
        {
            continue;
        }

        auto ext = entry.path().extension();
        if (ext != ".json")
        {
            continue;
        }

        std::string lang_name = entry.path().stem().u8string();
        if (!lang_name.empty())
        {
            language_list_.push_back(lang_name);
        }
    }

    error_string_.clear();
    return true;
}

const std::string &LanguageManager::getString(const std::string &id_utf8) const
{
    auto it = language_dictionary_.find(id_utf8);
    if (it != language_dictionary_.end())
    {
        return it->second;
    }

    return missing_string_;
}

const std::string &LanguageManager::getLanguageName() const
{
    return language_name_;
}

const std::string &LanguageManager::getLastError() const
{
    return error_string_;
}

const std::vector<std::string> &LanguageManager::getLanguagesList() const
{
    return language_list_;
}