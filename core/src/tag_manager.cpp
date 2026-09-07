#include "tag_manager.h"

TagLibrary::TagLibrary(const std::filesystem::path &file_path_utf8) : config_path_(file_path_utf8)
{
    if (!loadTagsFromFile(file_path_utf8))
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

TagLibrary::~TagLibrary()
{
    saveTagsToFile();
}

bool TagLibrary::loadTagsFromFile(const std::filesystem::path &file_path_utf8)
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

    type_tags_.clear();
    type_color_.clear();
    config_path_ = file_path_utf8;

    if (config.contains("groups") && config["groups"].is_array())
    {
        for (const auto &group_obj : config["groups"])
        {
            std::string type = group_obj.value("type", "");
            if (type.empty())
            {
                continue;
            }

            std::string color = group_obj.value("color", "#FFC0CB");
            if (!type_color_.count(type) && isValidHexColor(color))
            {
                type_color_[type] = color;
            }
            else if (!isValidHexColor(color))
            {
                type_color_[type] = "#FFC0CB";
            }

            // 无论该类型是否有标签 都在 type_tags_ 登记（空类型也保留）
            auto &tag_vec = type_tags_[type];
            if (group_obj.contains("tags") && group_obj["tags"].is_array())
            {
                for (const auto &tag_obj : group_obj["tags"])
                {
                    if (tag_obj.is_string())
                    {
                        std::string tag = tag_obj.get<std::string>();
                        if (!tag.empty())
                        {
                            tag_vec.push_back(tag);
                        }
                    }
                }
            }
        }
        clearInvalidTag();
        error_string_.clear();
        return true;
    }

    error_string_ = "[tip] JSON is empty";
    return true;
}

bool TagLibrary::saveTagsToFile() const
{
    if (config_path_.empty())
    {
        error_string_ = "[warning] No config file path has been set";
        return false;
    }

    nlohmann::json root;
    root["groups"] = nlohmann::json::array();

    // 以 type_color_ 的键为准遍历(类型以颜色确认存在) 保证空类型(tags 为 0)也会被保存
    for (const auto &type : getAllTypeNames())
    {
        nlohmann::json group_obj;
        group_obj["type"] = type;

        auto color_it = type_color_.find(type);
        group_obj["color"] = (color_it != type_color_.end()) ? color_it->second : "";

        auto tags_it = type_tags_.find(type);
        if (tags_it != type_tags_.end())
        {
            group_obj["tags"] = tags_it->second;
        }
        else
        {
            group_obj["tags"] = nlohmann::json::array();
        }
        root["groups"].push_back(group_obj);
    }

    std::error_code ec;
    auto temp_path = config_path_;
    temp_path += ".tmp";

    std::ofstream file(temp_path, std::ios::binary);
    if (!file.is_open())
    {
        error_string_ = "[warning] Cannot open temp file for writing: " + temp_path.u8string();
        return false;
    }

    try
    {
        file << root.dump(4);
        file.flush();
        if (!file.good())
        {
            error_string_ = "[error] Write failed";
            throw std::runtime_error("Write failed");
        }
        file.close();
    }
    catch (const std::exception &e)
    {
        error_string_ = "[warning] Failed to write JSON: " + std::string(e.what());
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    std::filesystem::rename(temp_path, config_path_, ec);
    if (ec)
    {
        error_string_ = "[warning] Failed to rename temp file: " + ec.message();
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagLibrary::isValidHexColor(const std::string &str)
{
    if (str.empty())
    {
        return false;
    }

    size_t start = 0;
    if (str[0] == '#')
    {
        start = 1;
    }

    size_t len = str.size() - start;
    if (len != 3 && len != 4 && len != 6 && len != 8)
    {
        return false;
    }

    for (size_t i = start; i < str.size(); i++)
    {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }
    return true;
}

bool TagLibrary::addTag(const std::string &tag, const std::string &type)
{
    if (hasTag(tag))
    {
        error_string_.clear();
        return true;
    }
    if (!hasType(type))
    {
        error_string_ = "[warning] type is not found ";
        return false;
    }

    type_tags_[type].push_back(tag);
    error_string_.clear();
    return true;
}

bool TagLibrary::addType(const std::string &type, std::string &color)
{
    if (hasType(type))
    {
        error_string_.clear();
        return true;
    }
    if (!isValidHexColor(color))
    {
        error_string_ = "[tip] color format error ";
        color = "#FFC0CB";
    }

    type_color_[type] = color;
    type_tags_[type]; // 登记空类型：0 标签的类型同样存在（保持 type_tags_ 与 type_color_ 键一致）
    error_string_.clear();
    return true;
}

bool TagLibrary::removeTag(const std::string &tag)
{
    for (auto &pair : type_tags_)
    {
        std::vector<std::string> &vec = pair.second;
        auto it = std::find(vec.begin(), vec.end(), tag);
        if (it != vec.end())
        {
            vec.erase(it);
            error_string_.clear();
            return true;
        }
    }

    error_string_ = "[warning] tag is not found ";
    return false;
}

bool TagLibrary::removeType(const std::string &type)
{
    auto color_it = type_color_.find(type);
    if (color_it == type_color_.end())
    {
        error_string_ = "[tip] type not found";
        return false;
    }

    type_color_.erase(color_it);

    auto tags_it = type_tags_.find(type);
    if (tags_it != type_tags_.end())
    {
        type_tags_.erase(tags_it);
    }

    error_string_.clear();
    return true;
}

bool TagLibrary::renameTag(const std::string &old_tag, const std::string &new_tag)
{
    if (old_tag.empty() || new_tag.empty())
    {
        error_string_ = "[warning] Tag name cannot be empty";
        return false;
    }
    if (old_tag == new_tag)
    {
        return true;
    }
    if (hasTag(new_tag))
    {
        error_string_ = "[warning] New tag already exists: " + new_tag;
        return false;
    }

    std::string found_type;
    for (auto &[type, tags] : type_tags_)
    {
        auto it = std::find(tags.begin(), tags.end(), old_tag);
        if (it != tags.end())
        {
            found_type = type;
            *it = new_tag;
            break;
        }
    }
    if (found_type.empty())
    {
        error_string_ = "[warning] Tag not found: " + old_tag;
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagLibrary::renameType(const std::string &old_type, const std::string &new_type)
{
    if (old_type.empty() || new_type.empty())
    {
        error_string_ = "[warning] Type name cannot be empty";
        return false;
    }
    if (old_type == new_type)
    {
        return true;
    }
    if (hasType(new_type))
    {
        error_string_ = "[warning] New type already exists: " + new_type;
        return false;
    }

    auto it_tags = type_tags_.find(old_type);
    if (it_tags == type_tags_.end())
    {
        error_string_ = "[warning] Type not found: " + old_type;
        return false;
    }

    auto it_color = type_color_.find(old_type);
    type_tags_[new_type] = std::move(it_tags->second);
    type_tags_.erase(it_tags);

    if (it_color != type_color_.end())
    {
        type_color_[new_type] = std::move(it_color->second);
        type_color_.erase(it_color);
    }

    error_string_.clear();
    return true;
}

bool TagLibrary::hasType(const std::string &type) const
{
    return type_color_.find(type) != type_color_.end();
}

bool TagLibrary::hasTag(const std::string &tag) const
{
    for (const auto &pair : type_tags_)
    {
        const auto &vec = pair.second;
        if (std::find(vec.begin(), vec.end(), tag) != vec.end())
        {
            error_string_.clear();
            return true;
        }
    }
    return false;
}

void TagLibrary::clearInvalidTag()
{
    std::unordered_set<std::string> seen_tags;
    bool changed = false;

    for (auto &pair : type_tags_)
    {
        std::vector<std::string> &vec = pair.second;
        std::vector<std::string> filtered;
        filtered.reserve(vec.size());

        for (const auto &tag : vec)
        {
            if (tag.empty())
            {
                changed = true;
                continue;
            }

            if (seen_tags.insert(tag).second)
            {
                filtered.push_back(tag);
            }
            else
            {
                changed = true;
            }
        }
        vec.swap(filtered);
    }
}

bool TagLibrary::setTypeColor(const std::string &type, const std::string &new_color)
{
    if (!hasType(type))
    {
        error_string_ = "[warning] Type not found: " + type;
        return false;
    }
    if (!isValidHexColor(new_color))
    {
        error_string_ = "[warning] Invalid color format: " + new_color;
        return false;
    }

    type_color_[type] = new_color;
    error_string_.clear();
    return true;
}

bool TagLibrary::setTagType(const std::string &tag, const std::string &new_type)
{
    if (!hasTag(tag))
    {
        error_string_ = "Tag not found: " + tag;
        return false;
    }
    if (!hasType(new_type))
    {
        error_string_ = "Type not found: " + new_type;
        return false;
    }

    for (auto &[type, tags] : type_tags_)
    {
        auto it = std::find(tags.begin(), tags.end(), tag);
        if (it != tags.end())
        {
            tags.erase(it);
            break;
        }
    }

    type_tags_[new_type].push_back(tag);
    error_string_.clear();
    return true;
}

std::string TagLibrary::getColorByType(const std::string &type) const
{
    auto it = type_color_.find(type);
    if (it != type_color_.end())
    {
        return it->second;
    }
    else
    {
        error_string_ = "[warning] type is not found";
        return "";
    }
}

std::string TagLibrary::getTypeOfTag(const std::string &tag) const
{
    for (const auto &pair : type_tags_)
    {
        const auto &vec = pair.second;
        if (std::find(vec.begin(), vec.end(), tag) != vec.end())
        {
            return pair.first;
        }
    }

    error_string_ = "[warning] tag is not found";
    return "";
}

std::vector<std::string> TagLibrary::getAllTypeNames() const
{
    std::vector<std::string> result;
    result.reserve(type_color_.size());
    for (const auto &pair : type_color_)
    {
        result.push_back(pair.first);
    }

    return result;
}

std::vector<std::string> TagLibrary::getAllTagNames() const
{
    std::vector<std::string> result;
    for (const auto &pair : type_tags_)
    {
        const auto &tags = pair.second;
        result.insert(result.end(), tags.begin(), tags.end());
    }

    return result;
}

const std::unordered_map<std::string, std::vector<std::string>> &TagLibrary::getTypeTag() const
{
    return type_tags_;
}

const std::unordered_map<std::string, std::string> &TagLibrary::getTypeColor() const
{
    return type_color_;
}

const std::filesystem::path &TagLibrary::getLoadPath() const
{
    return config_path_;
}

const std::string &TagLibrary::getLastError() const
{
    return error_string_;
}

std::vector<std::string> TagLibrary::autoComplete(const std::string &prefix) const
{
    std::vector<std::string> result;
    if (prefix.empty())
    {
        return result;
    }

    for (const auto &pair : type_tags_)
    {
        for (const auto &tag : pair.second)
        {
            if (tag.compare(0, prefix.size(), prefix) == 0)
            {
                result.push_back(tag);
            }
        }
    }
    return result;
}

TagFileManager::TagFileManager(const StoreMode default_mode) : default_mode_(default_mode)
{
}

TagFileManager::~TagFileManager()
{
}

void TagFileManager::setDefaultMode(const StoreMode mode)
{
    default_mode_ = mode;
}

const TagFileManager::StoreMode &TagFileManager::getDefaultMode() const
{
    return default_mode_;
}

const std::string &TagFileManager::getLastError() const
{
    return error_string_;
}

bool TagFileManager::addTag(const std::filesystem::path &file_path_utf8, const std::string &tag)
{
    if (tag.empty())
    {
        error_string_.clear();
        return true;
    }

    std::vector<std::string> tags = extractTags(file_path_utf8, default_mode_);
    if (std::find(tags.begin(), tags.end(), tag) != tags.end())
    {
        error_string_.clear();
        return true;
    }

    tags.push_back(tag);
    if (writeTagsToFile(file_path_utf8, tags, default_mode_))
    {
        error_string_.clear();
        return true;
    }

    error_string_ = "[warning] addition failed";
    return false;
}

bool TagFileManager::removeTag(const std::filesystem::path &file_path_utf8, const std::string &tag)
{
    if (tag.empty())
    {
        error_string_.clear();
        return true;
    }

    std::vector<std::string> tags = extractTags(file_path_utf8, default_mode_);
    auto it = std::find(tags.begin(), tags.end(), tag);

    if (it == tags.end())
    {
        error_string_.clear();
        return true;
    }

    tags.erase(it);
    if (writeTagsToFile(file_path_utf8, tags, default_mode_))
    {
        error_string_.clear();
        return true;
    }

    error_string_ = "[warning] removal failed";
    return false;
}

bool TagFileManager::removeTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags)
{
    if (tags.empty())
    {
        error_string_.clear();
        return true;
    }

    std::vector<std::string> current_tags = extractTags(file_path_utf8, default_mode_);
    std::sort(current_tags.begin(), current_tags.end());

    std::vector<std::string> sorted_remove = tags;
    std::sort(sorted_remove.begin(), sorted_remove.end());

    std::vector<std::string> new_tags;
    std::set_difference(current_tags.begin(), current_tags.end(), sorted_remove.begin(), sorted_remove.end(), std::back_inserter(new_tags));

    if (new_tags.size() == current_tags.size() && std::equal(new_tags.begin(), new_tags.end(), current_tags.begin()))
    {
        error_string_.clear();
        return true;
    }

    if (writeTagsToFile(file_path_utf8, new_tags, default_mode_))
    {
        error_string_.clear();
        return true;
    }

    error_string_ = "[warning] removal failed";
    return false;
}

bool TagFileManager::convertMode(const std::filesystem::path &file_path_utf8, StoreMode from_mode, StoreMode to_mode, bool keep_old)
{
    if (from_mode == to_mode && keep_old)
    {
        error_string_.clear();
        return true;
    }
    else
    {
        std::vector<std::string> tags = extractTags(file_path_utf8, from_mode);
        if (!writeTagsToFile(file_path_utf8, tags, to_mode))
        {
            error_string_ = "[warning] write failed";
            return false;
        }
    }

    if (!keep_old)
    {
        if (!removeModeTags(file_path_utf8, from_mode))
        {
            error_string_ = "[warning] remove old tags failed";
            return false;
        }
    }

    error_string_.clear();
    return true;
}

bool TagFileManager::removeModeTags(const std::filesystem::path &file_path_utf8, StoreMode mode)
{
    if (mode == StoreMode::Filename)
    {
        std::filesystem::path new_path = removeFilenameTagsPath(file_path_utf8);
        if (new_path != file_path_utf8)
        {
            std::error_code ec;
            std::filesystem::rename(file_path_utf8, new_path, ec);
            if (ec)
            {
                error_string_ = "[warning] Failed to rename file when removing filename tags: " + ec.message();
                return false;
            }
        }
    }
    else
    {
        std::filesystem::path sidecar_path = buildCleanSidecarPath(file_path_utf8);
        std::error_code ec;
        if (std::filesystem::exists(sidecar_path, ec))
        {
            std::filesystem::remove(sidecar_path, ec);
            if (ec)
            {
                error_string_ = "[warning] Failed to remove sidecar file: " + ec.message();
                return false;
            }
        }
    }

    error_string_.clear();
    return true;
}

std::vector<std::string> TagFileManager::extractTags(const std::filesystem::path &file_path_utf8, StoreMode mode) const
{
    std::error_code ec;
    if (std::filesystem::is_directory(file_path_utf8, ec) || mode == StoreMode::Sidecar)
    {
        std::filesystem::path sidecar_path = buildSidecarPath(file_path_utf8);
        std::vector<std::string> tags;

        if (readSidecar(sidecar_path, tags))
        {
            return tags;
        }
        return {};
    }
    else
    {
        return parseFromFilename(file_path_utf8);
    }
}

std::vector<std::string> TagFileManager::extractTags(const std::filesystem::path &file_path_utf8) const
{
    return extractTags(file_path_utf8, default_mode_);
}

std::filesystem::path TagFileManager::buildSidecarPath(const std::filesystem::path &file_path_utf8)
{
    std::filesystem::path parent = file_path_utf8.parent_path();
    std::filesystem::path file_name = file_path_utf8.filename();
    return parent / ".tag" / (file_name.u8string() + ".json");
}

std::filesystem::path TagFileManager::buildCleanSidecarPath(const std::filesystem::path &file_path_utf8)
{
    return buildSidecarPath(removeFilenameTagsPath(file_path_utf8));
}

std::vector<std::string> TagFileManager::parseFromFilename(const std::filesystem::path &file_name)
{
    std::vector<std::string> tags;
    std::string filename = file_name.filename().u8string();

    std::regex pattern(R"(\{\[([^\]]+)\]\})");
    std::smatch match;
    std::string::const_iterator search_start(filename.cbegin());

    while (std::regex_search(search_start, filename.cend(), match, pattern))
    {
        std::string tag_block = match[1].str();
        size_t pos = 0;
        size_t start = 0;

        while ((pos = tag_block.find(',', start)) != std::string::npos)
        {
            std::string tag = tag_block.substr(start, pos - start);
            if (!tag.empty())
            {
                tags.push_back(tag);
            }
            start = pos + 1;
        }

        std::string last_tag = tag_block.substr(start);
        if (!last_tag.empty())
        {
            tags.push_back(last_tag);
        }

        search_start = match.suffix().first;
    }

    return tags;
}

std::string TagFileManager::formatFilenameWithTags(const std::string &file_name, const std::vector<std::string> &tags)
{
    if (tags.empty())
    {
        return file_name;
    }

    std::string tag_str;
    for (size_t i = 0; i < tags.size(); i++)
    {
        if (i > 0)
        {
            tag_str += ",";
        }
        tag_str += tags[i];
    }

    return file_name + "{[" + tag_str + "]}";
}

std::string TagFileManager::removeTagsFromFilename(const std::string &file_name)
{
    std::regex pattern(R"(\{[^\}]*\})");
    return std::regex_replace(file_name, pattern, "");
}

std::filesystem::path TagFileManager::removeFilenameTagsPath(const std::filesystem::path &path)
{
    auto parent = path.parent_path();
    std::string clean_stem = removeTagsFromFilename(path.stem().u8string());
    std::string ext = path.extension().u8string();
    std::string clean_filename = clean_stem + ext;

    return parent / clean_filename;
}

bool TagFileManager::readSidecar(const std::filesystem::path &sidecar_path_utf8, std::vector<std::string> &tags)
{
    tags.clear();

    std::ifstream file(sidecar_path_utf8, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &)
    {
        return false;
    }

    if (!json.contains("tags") || !json["tags"].is_string())
    {
        return false;
    }

    std::string tag_str = json["tags"].get<std::string>();
    if (tag_str.empty())
    {
        return true;
    }

    size_t pos = 0, start = 0;
    while ((pos = tag_str.find(',', start)) != std::string::npos)
    {
        std::string tag = tag_str.substr(start, pos - start);
        if (!tag.empty())
        {
            tags.push_back(tag);
        }
        start = pos + 1;
    }

    std::string last_tag = tag_str.substr(start);
    if (!last_tag.empty())
    {
        tags.push_back(last_tag);
    }

    return true;
}

bool TagFileManager::writeSidecar(const std::filesystem::path &sidecar_path_utf8, const std::vector<std::string> &tags)
{
    std::error_code ec;
    auto parent = sidecar_path_utf8.parent_path();
    if (!std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            return false;
        }
    }

    std::string tag_str;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        if (i > 0)
            tag_str += ",";
        tag_str += tags[i];
    }

    nlohmann::json json;
    json["tags"] = tag_str;

    std::ofstream file(sidecar_path_utf8, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    try
    {
        file << json.dump(4);
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}

bool TagFileManager::writeTagsToFile(const std::filesystem::path &file_path, const std::vector<std::string> &tags, StoreMode mode)
{

    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec))
    {
        return false;
    }

    if (std::filesystem::is_directory(file_path) || mode == StoreMode::Sidecar)
    {
        std::filesystem::path sidecar_path = buildCleanSidecarPath(file_path);
        if (!writeSidecar(sidecar_path, tags))
        {
            return false;
        }

        return true;
    }
    else
    {
        auto parent = file_path.parent_path();
        std::string stem = file_path.stem().u8string();
        std::string ext = file_path.extension().u8string();

        std::string clean_stem = removeTagsFromFilename(stem);
        std::string new_stem = formatFilenameWithTags(clean_stem, tags);
        std::string new_filename = new_stem + ext;
        std::filesystem::path new_path = parent / new_filename;

        if (new_path == file_path)
        {
            return true;
        }

        std::filesystem::rename(file_path, new_path, ec);
        if (ec)
        {
            return false;
        }

        return true;
    }
}