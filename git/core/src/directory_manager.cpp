#include "directory_manager.h"

DirectoryConfigManager::DirectoryConfigManager(const std::filesystem::path &config_path_utf8) : config_path_(config_path_utf8)
{
    if (!loadFromFile(config_path_utf8))
    {
        std::filesystem::path dir_path = std::filesystem::path(config_path_).parent_path();
        if (!dir_path.empty() && !std::filesystem::exists(dir_path))
        {
            std::filesystem::create_directories(dir_path);
        }

        std::ofstream(config_path_, std::ios::out);
        error_string_ = "[warning] File not found";
    }
}

DirectoryConfigManager::~DirectoryConfigManager()
{
    saveToFile();
}

bool DirectoryConfigManager::loadFromFile(const std::filesystem::path &file_path_utf8)
{
    std::ifstream file(file_path_utf8, std::ios::binary);
    if (!file.is_open())
    {
        error_string_ = "[warning] Cannot open config file: " + file_path_utf8.u8string();
        return false;
    }

    nlohmann::json config;
    try
    {
        file >> config;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        error_string_ = "[warning] File format error: " + std::string(e.what());
        return false;
    }

    managed_directories_.clear();
    config_path_ = file_path_utf8;

    if (config.contains("managed_dirs") && config["managed_dirs"].is_array())
    {
        for (const auto &dir : config["managed_dirs"])
        {
            std::string utf8_original = dir.get<std::string>();
            std::filesystem::path canonical_path = formatPath(std::filesystem::path(utf8_original));

            Directory dirs;
            dirs.original_path_ = utf8_original;
            dirs.canonical_path_ = canonical_path;
            dirs.is_valid_ = isDirectoryValid(canonical_path);
            managed_directories_.push_back(dirs);
        }
        error_string_.clear();
        return true;
    }

    error_string_ = "[tip] JSON is empty";
    return true;
}

bool DirectoryConfigManager::saveToFile()
{
    if (config_path_.empty())
    {
        error_string_ = "[warning] No config file path has been set";
        return false;
    }

    nlohmann::json config;
    nlohmann::json dirs_array = nlohmann::json::array();

    for (const auto &dir : managed_directories_)
    {
        dirs_array.push_back(dir.canonical_path_);
    }

    config["managed_dirs"] = dirs_array;
    auto temp_path = config_path_;
    temp_path += ".tmp";
    std::ofstream file(temp_path, std::ios::binary);

    if (!file.is_open())
    {
        error_string_ = "[warning] Cannot open temporary file for writing: " + temp_path.u8string();
        return false;
    }

    try
    {
        file << config.dump(4);
        file.flush();
        if (!file.good())
        {
            error_string_ = "[error] Failed to write data to temporary file";
            throw std::runtime_error("Failed to write data to temporary file");
        }
        file.close();
    }
    catch (const std::exception &e)
    {
        error_string_ = "[warning] Failed to write config to temporary file: " + std::string(e.what());
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, config_path_, ec);
    if (ec)
    {
        error_string_ = "[warning] Failed to rename temporary file to config file: " + ec.message();
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    error_string_.clear();
    return true;
}

void DirectoryConfigManager::clearInvalidPath()
{
    for (auto &dir : managed_directories_)
    {
        dir.is_valid_ = isDirectoryValid(dir.canonical_path_);
    }

    managed_directories_.erase(
        std::remove_if(managed_directories_.begin(), managed_directories_.end(),
                       [](const Directory &dir)
                       {
                           return !dir.is_valid_;
                       }),
        managed_directories_.end());

    error_string_.clear();
}

bool DirectoryConfigManager::addDirectory(const std::filesystem::path &utf8_path)
{
    if (!isDirectoryValid(utf8_path))
    {
        error_string_ = "[warning] directory does not exist or no permission";
        return false;
    }

    auto canonical = formatPath(utf8_path);
    if (canonical.empty())
    {
        error_string_ = "[warning] failed to normalize path";
        return false;
    }

    auto normStr = canonical.u8string();

#ifdef _WIN32
    auto lowerStr = normStr;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    auto it = std::find_if(managed_directories_.begin(), managed_directories_.end(),
                           [&lowerStr](const Directory &d)
                           {
                               std::string dStr = d.canonical_path_.u8string();
                               std::transform(dStr.begin(), dStr.end(), dStr.begin(), [](unsigned char c)
                                              { return std::tolower(c); });
                               return dStr == lowerStr;
                           });
#else
    auto it = std::find_if(managed_directories_.begin(), managed_directories_.end(), [&normStr](const Directory &d)
                           { return d.canonical_path_.u8string() == normStr; });
#endif

    if (it != managed_directories_.end())
    {
        error_string_ = "[warning] directory already managed";
        return false;
    }

    Directory dirs;
    dirs.original_path_ = utf8_path.u8string();
    dirs.canonical_path_ = canonical;
    dirs.is_valid_ = true;
    managed_directories_.push_back(dirs);

    error_string_.clear();
    return true;
}

bool DirectoryConfigManager::removeDirectory(const std::filesystem::path &utf8_path)
{
    auto normInput = formatPath(utf8_path);
    auto it = std::find_if(managed_directories_.begin(), managed_directories_.end(), [&normInput](const Directory &dir)
                           { return dir.canonical_path_ == normInput; });

    if (it != managed_directories_.end())
    {
        managed_directories_.erase(it);
        error_string_.clear();
        return true;
    }

    error_string_ = "[tip] not found";
    return false;
}

bool DirectoryConfigManager::isPathAllowed(const std::filesystem::path &utf8_path) const
{
    for (const auto &dir : managed_directories_)
    {
        if (isPathUnderRoot(utf8_path, dir.canonical_path_) && dir.is_valid_)
        {
            error_string_.clear();
            return true;
        }
    }

    return false;
}

const std::string &DirectoryConfigManager::getLastError() const
{
    return error_string_;
}

std::filesystem::path DirectoryConfigManager::getConfigPath() const
{
    return config_path_;
}

const std::vector<Directory> &DirectoryConfigManager::getDirectories() const
{
    return managed_directories_;
}

const std::filesystem::path &DirectoryConfigManager::getLastValidDir() const
{
    for (auto it = managed_directories_.rbegin(); it != managed_directories_.rend(); ++it)
    {
        if (it->is_valid_)
        {
            return it->canonical_path_;
        }
    }
    return empty_;
}

const std::vector<std::filesystem::path> DirectoryConfigManager::getValidDirList() const
{
    std::vector<std::filesystem::path> result;
    result.reserve(managed_directories_.size());

    for (auto dir : managed_directories_)
    {
        if (dir.is_valid_)
        {
            result.push_back(dir.canonical_path_);
        }
    }
    return result;
}

bool DirectoryConfigManager::isDirectoryValid(const std::filesystem::path &path) const
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
    {
        error_string_ = "[warning] directory does not exist";
        return false;
    }

    if (!std::filesystem::is_directory(path, ec) || ec)
    {
        error_string_ = "[warning] path is not a directory";
        return false;
    }

    auto perms = std::filesystem::status(path, ec).permissions();
    if (ec)
    {
        error_string_ = "[warning] failed to get permissions";
        return false;
    }

    return (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none && (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
}

std::filesystem::path DirectoryConfigManager::formatPath(std::filesystem::path path)
{
    std::error_code ec;
    auto abs_path = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path;
    }

    auto norm = abs_path.lexically_normal();
    std::string str = norm.u8string();

    for (char &c : str)
    {
        if (c == '\\')
            c = '/';
    }
    if (str.size() > 1 && str.back() == '/')
    {
        str.pop_back();
    }

    return std::filesystem::u8path(str);
}

bool DirectoryConfigManager::isPathUnderRoot(const std::filesystem::path &path, const std::filesystem::path &root) const
{
    auto normPath = path.lexically_normal().generic_string();
    auto normRoot = root.lexically_normal().generic_string();

    if (!normRoot.empty() && normRoot.back() != '/')
        normRoot += '/';
    if (!normPath.empty() && normPath.back() != '/')
        normPath += '/';

#ifdef _WIN32
    std::transform(normPath.begin(), normPath.end(), normPath.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    std::transform(normRoot.begin(), normRoot.end(), normRoot.begin(), [](unsigned char c)
                   { return std::tolower(c); });
#endif

    return normPath.rfind(normRoot, 0) == 0;
}