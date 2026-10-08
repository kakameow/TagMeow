#include "tag_manager.h"
#include "system_error_text.h"

#include <chrono>

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
        error_string_ = "[warning] Failed to rename temp file: " + systemErrorText(ec);
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    error_string_.clear();
    return true;
}

// 合并另一个 tag.json 的内容到本库
// 一 本库为空(type_tags_/type_color_ 均为空): 直接用文件内容替换
// 二 类型(type 全局唯一): 只添加本库没有的 颜色非法则回落默认色 空类型同样登记
// 三 标签(tag 全局唯一): 把本库没有的 tag 添加到其所属 type 下
// 四 多余数据(重复标签/跨类型重名/空项/无效颜色)直接丢弃
bool TagLibrary::mergeTags(const std::filesystem::path &tag_json_path_utf8)
{
    error_string_.clear();

    if (!std::filesystem::exists(tag_json_path_utf8))
    {
        error_string_ = "[warning] file does not exist: " + tag_json_path_utf8.u8string();
        return false;
    }

    std::ifstream file(tag_json_path_utf8, std::ios::binary);
    if (!file.is_open())
    {
        error_string_ = "[warning] Cannot open config file: " + tag_json_path_utf8.u8string();
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

    if (!config.contains("groups") || !config["groups"].is_array())
    {
        error_string_ = "[tip] JSON is empty";
        return true; // 无 groups: 没有可合并的内容
    }

    // 先解析待合并数据(无效项在此丢弃)
    std::unordered_map<std::string, std::vector<std::string>> in_tags;
    std::unordered_map<std::string, std::string> in_color;

    for (const auto &group_obj : config["groups"])
    {
        if (!group_obj.is_object())
        {
            continue;
        }

        std::string type = group_obj.value("type", "");
        if (type.empty())
        {
            continue;
        }

        std::string color = group_obj.value("color", "#FFC0CB");
        if (!isValidHexColor(color))
        {
            color = "#FFC0CB";
        }
        if (in_color.find(type) == in_color.end())
        {
            in_color[type] = color;
        }

        auto &tag_vec = in_tags[type]; // 登记空类型
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

    // 一 本库为空 -> 直接替换(统一去重保证全局唯一)
    if (type_tags_.empty() && type_color_.empty())
    {
        type_tags_ = std::move(in_tags);
        type_color_ = std::move(in_color);
        clearInvalidTag();
        error_string_.clear();
        return true;
    }

    // 二 类型: 只添加本库没有的
    for (const auto &pair : in_color)
    {
        const std::string &type = pair.first;
        if (!hasType(type))
        {
            type_color_[type] = isValidHexColor(pair.second) ? pair.second : "#FFC0CB";
            type_tags_[type]; // 空类型也要登记(保持 type_tags_ 与 type_color_ 键一致)
        }
    }

    // 三 标签: 只添加本库没有的(重复/跨类型重名 -> 丢弃)
    for (const auto &pair : in_tags)
    {
        auto it_type = type_tags_.find(pair.first);
        if (it_type == type_tags_.end())
        {
            continue; // 理论上不会出现(类型已在上面补齐)
        }

        for (const auto &tag : pair.second)
        {
            if (!hasTag(tag))
            {
                it_type->second.push_back(tag);
            }
        }
    }

    // 四 clearInvalidTag 兜底: 清理空项与(替换分支遗留的)重名
    clearInvalidTag();

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

    // 带上具体原因 不然界面只看到「删除失败」不知道为什么
    const std::string write_error = error_string_;
    error_string_ = "[warning] removal failed: " + write_error;
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

    // 带上具体原因 不然界面只看到「删除失败」不知道为什么
    const std::string write_error = error_string_;
    error_string_ = "[warning] removal failed: " + write_error;
    return false;
}

bool TagFileManager::convertMode(const std::filesystem::path &file_path_utf8, StoreMode from_mode, StoreMode to_mode, bool keep_old)
{
    if (from_mode == to_mode && keep_old)
    {
        error_string_.clear();
        return true;
    }

    std::vector<std::string> tags = extractTags(file_path_utf8, from_mode);

    // 目标模式是 Filename 时 把文件名里已有的标签并进来（去重）
    // 否则 Sidecar -> Filename「保留旧数据」会把原文件名里残留的标签丢掉
    if (to_mode == StoreMode::Filename)
    {
        for (const std::string &tag : parseFromFilename(file_path_utf8))
        {
            if (std::find(tags.begin(), tags.end(), tag) == tags.end())
            {
                tags.push_back(tag);
            }
        }
    }

    if (!writeTagsToFile(file_path_utf8, tags, to_mode))
    {
        error_string_ = "[warning] write failed";
        return false;
    }

    // 写入可能改了文件名（Filename 模式）-> 删旧格式要用改名之后的路径
    const std::filesystem::path written_path = (to_mode == StoreMode::Filename && !tags.empty()) ? buildTaggedPath(file_path_utf8, tags) : file_path_utf8;

    if (!keep_old)
    {
        if (!removeModeTags(written_path, from_mode))
        {
            const std::string remove_error = error_string_;
            error_string_ = "[warning] remove old tags failed: " + remove_error;
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
        // 整名作用域（{[test]}.mp4）：去掉作用域之后连名字都没了
        // 先在名字前面补一个毫秒时间戳当名字部分（原名称原样跟在后面）再照常去作用域
        // 只有真要改名字时才补 纯读取 / 索引一个字节都不动
        std::filesystem::path target_path = file_path_utf8;
        const std::string materialized = materializeWholeNameBlock(file_path_utf8);

        if (materialized != file_path_utf8.filename().u8string())
        {
            target_path = file_path_utf8.parent_path() / materialized;
        }

        std::filesystem::path new_path = removeFilenameTagsPath(target_path);

        if (new_path != file_path_utf8)
        {
            std::error_code ec;
            std::filesystem::rename(file_path_utf8, new_path, ec);

            if (ec)
            {
                error_string_ = "[warning] Failed to rename file when removing filename tags: " + systemErrorText(ec);
                return false;
            }

            // 名字变了 侧车跟着搬
            if (!moveSidecar(file_path_utf8, new_path))
            {
                error_string_ = "[warning] Failed to move sidecar file: " + buildSidecarPath(new_path).u8string();
                return false;
            }
        }
    }
    else
    {
        // 侧车按真实文件名定位（与读取端一致）；名字刚被改过时旧数据可能还在"无标签名"的位置 兜底一起清
        const std::filesystem::path sidecar_path = buildSidecarPath(file_path_utf8);
        const std::filesystem::path clean_sidecar_path = buildCleanSidecarPath(file_path_utf8);

        if (!removeSidecar(sidecar_path))
        {
            error_string_ = "[warning] Failed to remove sidecar file: " + sidecar_path.u8string();
            return false;
        }

        if (clean_sidecar_path != sidecar_path && !removeSidecar(clean_sidecar_path))
        {
            error_string_ = "[warning] Failed to remove sidecar file: " + clean_sidecar_path.u8string();
            return false;
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

// 默认模式读取: 当前模式读不到标签时回退另一模式(兜底)
// 场景: Filename 模式读取时文件尚未写入标签块但存在 sidecar; 或 Sidecar 模式读取时
//       文件是历史 Filename 命名(名字里带标签)显式传 mode 的重载保持严格不兜底
std::vector<std::string> TagFileManager::extractTags(const std::filesystem::path &file_path_utf8) const
{
    std::vector<std::string> tags = extractTags(file_path_utf8, default_mode_);
    if (!tags.empty())
    {
        return tags;
    }

    const StoreMode other_mode = (default_mode_ == StoreMode::Sidecar) ? StoreMode::Filename : StoreMode::Sidecar;
    if (other_mode == StoreMode::Filename)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(file_path_utf8, ec))
        {
            return tags; // 目录没有 Filename 形式 不必回退
        }
    }

    std::vector<std::string> backup = extractTags(file_path_utf8, other_mode);
    if (!backup.empty())
    {
        return backup;
    }
    return tags; // 两种模式都没有 -> 返回空
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

// 整个名字（扩展名之前）就是一个 {[..]} 作用域 例如 {[test]}.mp4
bool TagFileManager::isWholeNameTagBlock(const std::filesystem::path &file_path_utf8)
{
    const std::string stem = file_path_utf8.stem().u8string();

    return !stem.empty() && removeTagsFromFilename(stem).empty();
}

// 整名作用域的文件没有能当"名字"的部分：{[test]}.mp4 去掉作用域就什么都不剩
// 这种在真要改名字时补一个毫秒时间戳前缀： <毫秒时间戳> + 原名称
// 前缀只能加在最前面：作用域必须留在名字结尾 加在后面以后就再也去不掉了
// 纯读取 / 索引 / 同步扫描都不会走到这里 文件不会因为"被读了一下"就改名
std::string TagFileManager::materializeWholeNameBlock(const std::filesystem::path &file_path_utf8)
{
    const std::string name = file_path_utf8.filename().u8string();

    if (!isWholeNameTagBlock(file_path_utf8))
    {
        return name;
    }

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(millis) + name;
}

// 与 writeTagsToFile 的 Filename 分支保持同一规则: 去掉旧标签块后按 tags 重新拼接
std::filesystem::path TagFileManager::buildTaggedPath(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags)
{
    // 整名作用域先补时间戳前缀 与 writeTagsToFile 的实际改名保持一致 预测路径才不会落空
    const std::filesystem::path target_path = file_path_utf8.parent_path() / materializeWholeNameBlock(file_path_utf8);
    std::filesystem::path parent = target_path.parent_path();
    std::string stem = target_path.stem().u8string();
    std::string ext = target_path.extension().u8string();
    std::string clean_stem = removeTagsFromFilename(stem);
    std::string new_stem = formatFilenameWithTags(clean_stem, tags);
    return parent / (new_stem + ext);
}

std::vector<std::string> TagFileManager::parseFromFilename(const std::filesystem::path &file_name)
{
    std::vector<std::string> tags;
    std::string filename = file_name.filename().u8string();

    // 读取宽松：名字里任意位置的 {[..]} 都算作用域（可以出现多个）
    // 但必须是 { + [ + 内容 + ] + } 双重包裹；单层的 {} / [] / {] 之类一律不算标签
    std::regex pattern(R"(\{\[([^\]\}]*)\]\})");
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
            if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end())
            {
                tags.push_back(tag);
            }
            start = pos + 1;
        }

        std::string last_tag = tag_block.substr(start);
        if (!last_tag.empty() && std::find(tags.begin(), tags.end(), last_tag) == tags.end())
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
    if (file_name.empty())
    {
        return file_name;
    }

    // 只剥离"文件尾"的双重包裹标签作用域（与 android 端 6130d93 收紧后的规则一致）：
    // 单层大括号（10{df}.mp4 的 {df}）和名字中间的 {[..]} 都要原样保留 一个字符都不能动
    // 连续多个（file{[a]}{[b]}.txt）循环剥干净
    static const std::regex tail_pattern(R"(\{\[[^\}\]]*\]\}(?=(\.[^.]*)?$))");
    std::string current = file_name;
    std::string next = std::regex_replace(current, tail_pattern, "");

    while (next != current)
    {
        current = next;
        next = std::regex_replace(current, tail_pattern, "");
    }

    return current;
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
    for (size_t i = 0; i < tags.size(); i++)
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

// 文件名变化时把侧车一起搬过去（.tag/<旧名>.json -> .tag/<新名>.json）
// 与 writeSidecar 一样是静态的：失败只返回 false 具体信息由调用方补
bool TagFileManager::moveSidecar(const std::filesystem::path &from_path_utf8, const std::filesystem::path &to_path_utf8)
{
    const std::filesystem::path from_sidecar = buildSidecarPath(from_path_utf8);
    const std::filesystem::path to_sidecar = buildSidecarPath(to_path_utf8);

    if (from_sidecar == to_sidecar)
    {
        return true;
    }

    std::error_code ec;

    if (!std::filesystem::exists(from_sidecar, ec))
    {
        return true;
    }

    const std::filesystem::path to_dir = to_sidecar.parent_path();

    if (!to_dir.empty() && !std::filesystem::exists(to_dir, ec))
    {
        std::filesystem::create_directories(to_dir, ec);

        if (ec)
        {
            return false;
        }
    }

    std::filesystem::rename(from_sidecar, to_sidecar, ec);

    if (ec)
    {
        return false;
    }

    removeSidecar(from_sidecar); // 旧 .tag 目录空了就顺手删掉

    return true;
}

bool TagFileManager::removeSidecar(const std::filesystem::path &sidecar_path_utf8)
{
    std::error_code ec;

    if (std::filesystem::exists(sidecar_path_utf8, ec))
    {
        std::filesystem::remove(sidecar_path_utf8, ec);

        if (ec)
        {
            // 静态方法碰不到 error_string_ 具体错误信息由调用方补
            return false;
        }
    }

    const std::filesystem::path sidecar_dir = sidecar_path_utf8.parent_path();

    if (!sidecar_dir.empty() && std::filesystem::exists(sidecar_dir, ec))
    {
        std::error_code remove_ec;
        std::filesystem::remove(sidecar_dir, remove_ec); // 目录非空会失败 属正常情况
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
        // 侧车按真实文件名存放（与读取端严格一致）
        std::filesystem::path sidecar_path = buildSidecarPath(file_path);

        // 标签为空时不写空壳 sidecar：删掉已有记录（同时清掉空的 .tag 目录）
        if (tags.empty())
        {
            return removeSidecar(sidecar_path);
        }

        if (!writeSidecar(sidecar_path, tags))
        {
            return false;
        }

        return true;
    }
    else
    {
        // 标签为空时不动文件名：转换时"源里没标签"是正常情况 不能顺手把名字里原有的内容抹掉
        if (tags.empty())
        {
            return true;
        }

        // 整名作用域：本来就没有名字部分 先补一个毫秒时间戳前缀再写标签
        // （前缀只能加在最前面 作用域必须留在名字结尾 加在后面以后就再也去不掉了）
        const std::filesystem::path target_path = file_path.parent_path() / materializeWholeNameBlock(file_path);
        auto parent = target_path.parent_path();
        std::string stem = target_path.stem().u8string();
        std::string ext = target_path.extension().u8string();

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

        // 名字变了 侧车跟着搬
        moveSidecar(file_path, new_path);

        return true;
    }
}