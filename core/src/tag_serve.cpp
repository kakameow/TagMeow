#include "tag_serve.h"

TagServe::TagServe(const std::vector<std::filesystem::path> &root_list_utf8, TagFileManager::StoreMode default_mode, std::filesystem::path tag_path_utf8, std::filesystem::path db_path_utf8)
    : root_list_(root_list_utf8), tag_file_(default_mode), tag_list_(tag_path_utf8), db_(db_path_utf8)
{
}

TagServe::~TagServe()
{
    saveTag();
}

bool TagServe::addRoot(std::filesystem::path root_path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (std::find(root_list_.begin(), root_list_.end(), root_path_utf8) != root_list_.end())
    {
        error_string_ = "[warning] Root already exists: " + root_path_utf8.u8string();
        return false;
    }

    auto extractor = [this](const std::filesystem::path &p)
    {
        return tag_file_.extractTags(p);
    };

    if (!db_.updateDirectory(root_path_utf8, extractor))
    {
        error_string_ = "Failed to update database for root: " + db_.getLastError();
        return false;
    }

    root_list_.push_back(root_path_utf8);
    error_string_.clear();
    return true;
}

bool TagServe::removeRoot(std::filesystem::path root_path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(root_list_.begin(), root_list_.end(), root_path_utf8);
    if (it == root_list_.end())
    {
        error_string_ = "[warning] Root not found: " + root_path_utf8.u8string();
        return false;
    }

    if (!db_.removeDirectory(root_path_utf8))
    {
        error_string_ = "Failed to remove directory from database: " + db_.getLastError();
        return false;
    }

    root_list_.erase(it);
    error_string_.clear();
    return true;
}

bool TagServe::reLoadRoot(std::vector<std::filesystem::path> root_list_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto &old_root : root_list_)
    {
        if (!db_.removeDirectory(old_root))
        {
            error_string_ = "Failed to remove old root data: " + db_.getLastError();
            return false;
        }
    }

    root_list_ = std::move(root_list_utf8);
    auto extractor = [this](const std::filesystem::path &p)
    {
        return tag_file_.extractTags(p);
    };

    size_t err = 0;
    for (const auto &dir : root_list_)
    {
        if (!db_.updateDirectory(dir, extractor))
        {
            err++;
        }
    }

    db_.clearRepeat();
    db_.cleanupInvalid();

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " root(s) failed to update";
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagServe::reLoadRoot(std::filesystem::path root_list_utf8)
{
    return reLoadRoot(std::vector<std::filesystem::path>{root_list_utf8});
}

bool TagServe::reLoadDB(std::filesystem::path db_path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.reload(db_path_utf8);
}

bool TagServe::reLoadTag(std::filesystem::path tag_path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.loadTagsFromFile(tag_path_utf8);
}

bool TagServe::saveTag()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.saveTagsToFile();
}

bool TagServe::addTag(const std::string &type, const std::string &tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.addTag(tag, type);
}

bool TagServe::addTag(const std::string &type, const std::vector<std::string> &tags)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t err = 0;

    for (auto tag : tags)
    {
        if (!tag_list_.addTag(tag, type))
        {
            err++;
        }
    }

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " tag(s) add failed";
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagServe::addType(const std::string &type, std::string &color)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.addType(type, color);
}

bool TagServe::removeTag(const std::string &tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.removeTag(tag);
}

bool TagServe::removeTag(const std::vector<std::string> &tags)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t err = 0;

    for (auto tag : tags)
    {
        if (!tag_list_.removeTag(tag))
        {
            err++;
        }
    }

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " tag(s) add failed";
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagServe::removeType(const std::string &type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.removeType(type);
}

bool TagServe::removeType(const std::vector<std::string> &types)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t err = 0;

    for (auto type : types)
    {
        if (!tag_list_.removeType(type))
        {
            err++;
        }
    }

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " type(s) add failed";
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagServe::renameTag(const std::string &old_tag, const std::string &new_tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.renameTag(old_tag, new_tag);
}

bool TagServe::renameType(const std::string &old_type, const std::string &new_type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.renameType(old_type, new_type);
}

bool TagServe::setTypeColor(const std::string &type, const std::string &new_color)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.setTypeColor(type, new_color);
}

bool TagServe::setTagType(const std::string &tag, const std::string &new_type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.setTagType(tag, new_type);
}

std::string TagServe::getColorByType(const std::string &type) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getColorByType(type);
}

std::string TagServe::getColorByTag(const std::string &tag) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getColorByType(tag_list_.getTypeOfTag(tag));
}

std::string TagServe::getTypeOfTag(const std::string &tag) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getTypeOfTag(tag);
}

std::unordered_map<std::string, std::vector<std::string>> TagServe::getTypeTag() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getTypeTag();
}

std::unordered_map<std::string, std::string> TagServe::getTypeColor() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getTypeColor();
}

bool TagServe::addFileTag(const std::filesystem::path &file_path_utf8, const std::string &tag)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_path_utf8.stem() == ".tag" || file_path_utf8.stem().empty())
    {
        error_string_ = "[warning] the tag directory cannot recommend or empty";
        return false;
    }

    for (auto dir : root_list_)
    {
        if (dir == file_path_utf8)
        {
            error_string_ = "[warning] the root directory cannot recommend";
            return false;
        }
    }

    if (!tag_list_.hasTag(tag))
    {
        error_string_ = "[warning] tag or path does not exist";
        return false;
    }

    // Filename 模式下标签写在文件名里 -> 先算出改名后的路径
    std::filesystem::path new_path = file_path_utf8;
    if (tag_file_.getDefaultMode() == TagFileManager::StoreMode::Filename && !std::filesystem::is_directory(file_path_utf8))
    {
        // 预判必须与写入同源: 严格按当前默认模式读取(不兜底) 否则算出的名字与实际写入不符
        auto tags = tag_file_.extractTags(file_path_utf8, tag_file_.getDefaultMode());
        if (std::find(tags.begin(), tags.end(), tag) == tags.end())
        {
            tags.push_back(tag);
        }
        new_path = TagFileManager::buildTaggedPath(file_path_utf8, tags);
    }

    if (!tag_file_.addTag(file_path_utf8, tag))
    {
        error_string_ = "[warning] addition failed";
        return false;
    }

    // 数据库按路径索引: 以磁盘实际状态判断是否改名后同步(同一把锁内 保证一致)
    syncAfterTagWriteNoLock(file_path_utf8, new_path);

    error_string_.clear();
    return true;
}

bool TagServe::addFileTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t err = 0;

    const bool rename_mode = (tag_file_.getDefaultMode() == TagFileManager::StoreMode::Filename) && !std::filesystem::is_directory(file_path_utf8);
    std::filesystem::path cur_path = file_path_utf8;

    for (auto tag : tags)
    {
        if (file_path_utf8.stem() == ".tag" || file_path_utf8.stem().empty())
        {
            err++;
            error_string_ = "[warning] the tag directory cannot recommend or empty";
            continue;
        }

        for (auto dir : root_list_)
        {
            if (dir == file_path_utf8)
            {
                err++;
                error_string_ = "[warning] the root directory cannot recommend";
                break;
            }
        }

        if (tag_list_.hasTag(tag))
        {
            std::filesystem::path next_path = cur_path;
            if (rename_mode)
            {
                auto cur_tags = tag_file_.extractTags(cur_path, tag_file_.getDefaultMode());
                if (std::find(cur_tags.begin(), cur_tags.end(), tag) == cur_tags.end())
                {
                    cur_tags.push_back(tag);
                }
                next_path = TagFileManager::buildTaggedPath(cur_path, cur_tags);
            }

            if (!tag_file_.addTag(cur_path, tag))
            {
                err++;
                error_string_ = "[warning] add failed";
                continue;
            }
            cur_path = next_path;
        }
    }

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " tag(s) add failed";
        return false;
    }

    syncAfterTagWriteNoLock(file_path_utf8, cur_path);

    error_string_.clear();
    return true;
}

bool TagServe::removeFileTag(const std::filesystem::path &file_path_utf8, const std::string &tag)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::filesystem::path new_path = file_path_utf8;
    if (tag_file_.getDefaultMode() == TagFileManager::StoreMode::Filename && !std::filesystem::is_directory(file_path_utf8))
    {
        auto tags = tag_file_.extractTags(file_path_utf8, tag_file_.getDefaultMode());
        auto it = std::find(tags.begin(), tags.end(), tag);
        if (it != tags.end())
        {
            tags.erase(it);
        }
        new_path = TagFileManager::buildTaggedPath(file_path_utf8, tags);
    }

    if (!tag_file_.removeTag(file_path_utf8, tag))
    {
        error_string_ = "[warning] removal failed";
        return false;
    }

    syncAfterTagWriteNoLock(file_path_utf8, new_path);

    error_string_.clear();
    return true;
}

bool TagServe::removeFileTag(const std::filesystem::path &file_path_utf8, const std::vector<std::string> &tags)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t err = 0;

    const bool rename_mode = (tag_file_.getDefaultMode() == TagFileManager::StoreMode::Filename) && !std::filesystem::is_directory(file_path_utf8);
    std::filesystem::path cur_path = file_path_utf8;

    for (const auto &tag : tags)
    {
        std::filesystem::path next_path = cur_path;
        if (rename_mode)
        {
            auto cur_tags = tag_file_.extractTags(cur_path, tag_file_.getDefaultMode());
            auto it = std::find(cur_tags.begin(), cur_tags.end(), tag);
            if (it != cur_tags.end())
            {
                cur_tags.erase(it);
            }
            next_path = TagFileManager::buildTaggedPath(cur_path, cur_tags);
        }

        if (!tag_file_.removeTag(cur_path, tag))
        {
            err++;
            error_string_ = "[warning] removal failed";
            continue;
        }
        cur_path = next_path;
    }

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " tag(s) remove failed";
        return false;
    }

    syncAfterTagWriteNoLock(file_path_utf8, cur_path);

    error_string_.clear();
    return true;
}

bool TagServe::convertMode(TagFileManager::StoreMode from_mode, TagFileManager::StoreMode to_mode, bool keep_old)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (from_mode == to_mode)
    {
        error_string_.clear();
        return true;
    }

    // 第一阶段 对所有文件执行 keep_old=true 只写入新格式不删除旧格式
    size_t file_failed_first = 0;
    size_t root_missing_count = 0;

    for (const auto &root : root_list_)
    {
        if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root))
        {
            root_missing_count++;
            continue;
        }

        std::error_code ec;
        std::filesystem::recursive_directory_iterator iter(root, ec);
        std::filesystem::recursive_directory_iterator end_iter;
        for (; iter != end_iter; ++iter)
        {
            if (ec)
            {
                error_string_ = "[warning] Iteration error: " + ec.message();
                return false;
            }
            if (iter->path().filename() == ".tag")
            {
                iter.disable_recursion_pending();
                continue;
            }
            if (iter->is_regular_file())
            {
                if (!tag_file_.convertMode(iter->path(), from_mode, to_mode, true))
                {
                    file_failed_first++;
                    if (file_failed_first == 1)
                    {
                        error_string_ = "First failure in first pass: " + iter->path().u8string() + " (" + tag_file_.getLastError() + ")";
                    }
                }
            }
        }
    }

    if (keep_old)
    {
        if (file_failed_first > 0)
        {
            error_string_ = "[warning] " + std::to_string(file_failed_first) + " file(s) failed to write new format. " + error_string_;
            return false;
        }
        if (root_missing_count > 0)
        {
            error_string_ = "[warning] " + std::to_string(root_missing_count) + " root(s) missing, but other roots processed.";
        }
        else
        {
            error_string_.clear();
        }
        tag_file_.setDefaultMode(to_mode);
        rebuildRootsNoLock(); // 转换会改写文件名 -> 数据库整根重建索引
        return true;
    }

    if (file_failed_first > 0)
    {
        error_string_ = "[warning] " + std::to_string(file_failed_first) + " file(s) failed in first pass (keep_old=true), aborting second pass.";
        return false;
    }

    // 第二阶段 对所有文件执行 keep_old=false 删除旧格式
    size_t file_failed_second = 0;
    for (const auto &root : root_list_)
    {
        if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root))
        {
            continue;
        }

        std::error_code ec;
        std::filesystem::recursive_directory_iterator iter(root, ec);
        std::filesystem::recursive_directory_iterator end_iter;
        for (; iter != end_iter; ++iter)
        {
            if (ec)
            {
                error_string_ = "[warning] Iteration error: " + ec.message();
                return false;
            }
            if (iter->path().filename() == ".tag")
            {
                iter.disable_recursion_pending();
                continue;
            }
            if (iter->is_regular_file())
            {
                if (!tag_file_.removeModeTags(iter->path(), from_mode))
                {
                    file_failed_second++;
                    if (file_failed_second == 1)
                    {
                        error_string_ = "First failure in second pass: " + iter->path().u8string() + " (" + tag_file_.getLastError() + ")";
                    }
                }
            }
        }
    }

    if (file_failed_second > 0)
    {
        error_string_ = "[warning] " + std::to_string(file_failed_second) + " file(s) failed in second pass (keep_old=false). " + "New data written, but some old data may remain.";
        return false;
    }

    tag_file_.setDefaultMode(to_mode);
    rebuildRootsNoLock(); // 转换会改写文件名 -> 数据库整根重建索引
    error_string_.clear();
    return true;
}

void TagServe::setDefaultMode(const TagFileManager::StoreMode mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tag_file_.setDefaultMode(mode);
}

const TagFileManager::StoreMode TagServe::getDefaultMode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_file_.getDefaultMode();
}

bool TagServe::updateRoots()
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto &old_root : root_list_)
    {
        if (!db_.removeDirectory(old_root))
        {
            error_string_ = "Failed to remove old root data: " + db_.getLastError();
            return false;
        }
    }

    auto extractor = [this](const std::filesystem::path &p)
    {
        return tag_file_.extractTags(p);
    };

    size_t err = 0;
    for (const auto &dir : root_list_)
    {
        if (!db_.updateDirectory(dir, extractor))
        {
            err++;
        }
    }

    db_.clearRepeat();
    db_.cleanupInvalid();

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " root(s) failed to update";
        return false;
    }

    error_string_.clear();
    return true;
}

bool TagServe::updateFile(const std::filesystem::path &file_path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return syncFileToDBNoLock(file_path_utf8);
}

// 内部无锁版本(调用方已持有 mutex_): 读取文件元数据与标签后写入数据库
bool TagServe::syncFileToDBNoLock(const std::filesystem::path &file_path_utf8)
{
    if (!std::filesystem::exists(file_path_utf8) || (!std::filesystem::is_regular_file(file_path_utf8) && !std::filesystem::is_directory(file_path_utf8)))
    {
        error_string_ = "[warning] Path does not exist or is not a regular file/directory: " + file_path_utf8.u8string();
        return false;
    }

    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(file_path_utf8, ec);
    auto size = std::filesystem::is_directory(file_path_utf8) ? 0 : std::filesystem::file_size(file_path_utf8, ec);

    if (ec)
    {
        error_string_ = "[warning] Failed to read file metadata: " + ec.message();
        return false;
    }

    auto file_now = std::filesystem::file_time_type::clock::now();
    auto sys_time = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(ftime - file_now);
    auto file_time = std::chrono::duration_cast<std::chrono::seconds>(sys_time.time_since_epoch()).count();
    auto tags = tag_file_.extractTags(file_path_utf8);
    std::string rel_path;
    std::string best_root_str;

    for (size_t i = 0; i < root_list_.size(); ++i)
    {
        const auto &root = root_list_[i];

        auto rel = std::filesystem::relative(file_path_utf8, root);
        if (!rel.empty() && rel.string().find("..") != 0)
        {
            if (best_root_str.empty() || root.string().length() > best_root_str.length())
            {
                best_root_str = root.string();
                rel_path = rel.generic_u8string();
            }
        }
    }

    int64_t sidecar_mtime = 0;
    if (tag_file_.getDefaultMode() == TagFileManager::StoreMode::Sidecar)
    {
        auto sidecar_path = TagFileManager::buildSidecarPath(file_path_utf8);
        if (std::filesystem::exists(sidecar_path))
        {
            auto sc_time = std::filesystem::last_write_time(sidecar_path, ec);
            if (!ec)
            {
                auto sc_now = std::filesystem::file_time_type::clock::now();
                auto sc_sys_time = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(sc_time - sc_now);
                sidecar_mtime = std::chrono::duration_cast<std::chrono::seconds>(sc_sys_time.time_since_epoch()).count();
            }
        }
    }

    table::FileInfo info;
    info.path_ = file_path_utf8.generic_u8string();
    info.rel_path_ = rel_path;
    info.file_mtime_ = file_time;
    info.file_size_ = static_cast<int64_t>(size);
    info.sidecar_mtime_ = sidecar_mtime;
    info.tags_ = tags;
    info.file_version_ = 1;
    info.last_refresh_time_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    if (!db_.updateFile(info))
    {
        error_string_ = "DB update failed: " + db_.getLastError();
        return false;
    }

    error_string_.clear();
    return true;
}

// 内部无锁版本: 用当前 root_list_ 重建数据库索引(updateDirectory 会先删同前缀旧行再重扫)
bool TagServe::rebuildRootsNoLock()
{
    auto extractor = [this](const std::filesystem::path &p)
    {
        return tag_file_.extractTags(p);
    };

    size_t err = 0;
    for (const auto &dir : root_list_)
    {
        if (!db_.updateDirectory(dir, extractor))
        {
            err++;
        }
    }

    db_.clearRepeat();
    db_.cleanupInvalid();

    if (err > 0)
    {
        error_string_ = "[warning] " + std::to_string(err) + " root(s) failed to update";
        return false;
    }

    error_string_.clear();
    return true;
}

const std::filesystem::path &TagServe::getLastFilePath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_file_path_;
}

// 标签写入成功后同步数据库(调用方已持锁)
// 以磁盘实际状态为准: 预测的改名路径存在 -> 删旧行(外键级联标签) + 写新行
// 否则说明没有发生改名(空操作: 标签已存在/不存在, 或文件名与规则不一致) -> 原地同步旧路径
// 注意: 不能只看预测路径不同就删旧行, 否则空操作时会误删数据库行
void TagServe::syncAfterTagWriteNoLock(const std::filesystem::path &old_path, const std::filesystem::path &predicted_path)
{
    std::error_code ec;
    const bool renamed = (predicted_path != old_path) && std::filesystem::exists(predicted_path, ec) && !ec;

    if (renamed)
    {
        db_.removeFile(old_path);
        syncFileToDBNoLock(predicted_path);
        last_file_path_ = predicted_path;
        return;
    }

    if (std::filesystem::exists(old_path, ec) && !ec)
    {
        syncFileToDBNoLock(old_path);
        last_file_path_ = old_path;
        return;
    }

    // 两个路径都不存在: 保留旧行不删(避免丢数据) 仅记录异常
    last_file_path_ = old_path;
    error_string_ = "[warning] tag written but the file path state is unexpected: " + old_path.u8string();
}

bool TagServe::removeFile(const std::filesystem::path &path_utf8)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.removeFile(path_utf8);
}

std::vector<table::FileInfo> TagServe::searchByTags(const FileDatabase::SearchOptions &opts) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.searchByTags(opts);
}

std::optional<table::FileInfo> TagServe::getFileInfo(const std::filesystem::path &path) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.getFileInfo(path);
}

std::string TagServe::getLastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_string_;
}

std::string TagServe::getDBError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.getLastError();
}

std::string TagServe::getTagError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_list_.getLastError();
}

std::string TagServe::getFileError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tag_file_.getLastError();
}