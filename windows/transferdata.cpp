#include "transferdata.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

// 统一使用 UTF-8

namespace
{
// 空值检测
bool isBlank(const std::string &s)
{
    for (char c : s)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            return false;
        }
    }
    return true;
}
} // namespace

TransferData::TransferData(DirectoryConfigManager *dm, TagServe *ts) : dm_(dm), ts_(ts)
{
}

TransferData::~TransferData() = default;

void TransferData::setBackend(DirectoryConfigManager *dm, TagServe *ts)
{
    dm_ = dm;
    ts_ = ts;
}

void TransferData::setOnDataChanged(std::function<void()> cb)
{
    on_data_changed_ = std::move(cb);
}

void TransferData::setSearchTags(const std::vector<std::string> &include, const std::vector<std::string> &exclude, const std::vector<std::string> &only)
{
    include_tags_ = include;
    exclude_tags_ = exclude;
    only_tags_ = only;
}

void TransferData::notify()
{
    if (on_data_changed_)
    {
        on_data_changed_();
    }
}

void TransferData::refreshDirList()
{
    path_list_.clear();
    if (!dm_)
    {
        return;
    }
    const auto dirs = dm_->getValidDirList();
    path_list_.reserve(dirs.size());
    for (const auto &d : dirs)
    {
        path_list_.push_back(d.generic_string()); // UTF-8 '/'
    }
}

void TransferData::refreshTagLibrary()
{
    type_tags_.clear();
    type_color_.clear();
    if (!ts_)
    {
        return;
    }
    type_tags_ = ts_->getTypeTag();
    type_color_ = ts_->getTypeColor();
}

// 从数据库按路径刷新 path_tags_
// filename 模式加/删标签会重命名文件(传入的可能是旧路径):
//   1) 先按传入路径查库 2) 查不到则用 TagServe::getLastFilePath() 的真实路径再查
//   3) 命中时把新路径"原位顶替"旧路径 -> UI 行顺序保持不变(不追加到末尾/不重排)
void TransferData::refreshFileTags(const std::string &path)
{
    if (!ts_)
    {
        return;
    }

    auto info = ts_->getFileInfo(std::filesystem::path(path));
    if (!info)
    {
        const auto &last = ts_->getLastFilePath();
        const std::string last_str = last.generic_u8string();
        if (!last_str.empty() && last_str != path)
        {
            info = ts_->getFileInfo(last);
        }
    }

    if (!info)
    {
        // 库中已无该路径: 清掉映射与顺序表里的残留
        path_tags_.erase(path);
        file_order_.erase(std::remove(file_order_.begin(), file_order_.end(), path), file_order_.end());
        return;
    }

    const std::string real = info->path_;
    if (real != path)
    {
        auto it = std::find(file_order_.begin(), file_order_.end(), path);
        if (it != file_order_.end())
        {
            *it = real; // 原位置顶替: 顺序不变
        }
        else
        {
            file_order_.push_back(real);
        }
        path_tags_.erase(path);
    }
    else if (std::find(file_order_.begin(), file_order_.end(), real) == file_order_.end())
    {
        file_order_.push_back(real);
    }

    path_tags_[real] = info->tags_;
}

bool TransferData::getSearch()
{
    if (!ts_)
    {
        return false;
    }

    FileDatabase::SearchOptions opts;
    opts.include_ = include_tags_; // 至少包含其中一个
    opts.exclude_ = exclude_tags_; // 排除
    opts.only_ = only_tags_;       // 只能包含(为空不限)

    const auto files = ts_->searchByTags(opts);
    path_tags_.clear();
    file_order_.clear();
    // 搜索与目录浏览是切换关系：搜索优先 结果覆盖列表并退出浏览模式
    browse_entries_.clear();
    browse_current_dir_.clear();
    for (const auto &f : files)
    {
        path_tags_[f.path_] = f.tags_;
        file_order_.push_back(f.path_); // 保留数据库返回顺序作为显示顺序
    }
    return true;
}

bool TransferData::getDirFile(const std::string &path)
{
    if (isBlank(path) || !dm_ || !ts_)
    {
        return false;
    }

    std::filesystem::path p(path);
    if (!dm_->isPathAllowed(p))
    {
        return false; // 不在沙盒(授权目录)内
    }

    // 从数据库取该目录前缀下的全部文件（统一按规范化绝对路径比较）
    const auto files = ts_->searchByTags({});
    path_tags_.clear();
    file_order_.clear();
    const std::string prefix = std::filesystem::absolute(p).lexically_normal().generic_string() + "/";
    for (const auto &f : files)
    {
        const std::string fp = std::filesystem::absolute(f.path_).lexically_normal().generic_string();
        if (fp.rfind(prefix, 0) == 0)
        {
            path_tags_[f.path_] = f.tags_;
            file_order_.push_back(f.path_);
        }
    }
    return true;
}

// 目录浏览（一层）：直接子项 + 数据库里的标签；不在授权根时最上面插入"返回上层"入口
bool TransferData::browseDir(const std::string &path)
{
    if (isBlank(path) || !dm_ || !ts_)
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path p(path);
    if (!dm_->isPathAllowed(p))
    {
        return false; // 不在沙盒(授权目录)内
    }

    const std::filesystem::path abs_dir = std::filesystem::absolute(p, ec).lexically_normal();
    if (ec || !std::filesystem::is_directory(abs_dir, ec) || ec)
    {
        return false;
    }

    browse_entries_.clear();
    browse_current_dir_ = abs_dir.generic_string();

    // 不在授权根目录时 最上面是返回上层的入口
    if (!isRootPath(browse_current_dir_))
    {
        BrowseEntry parent;
        parent.path_ = abs_dir.parent_path().generic_string();
        parent.name_ = "..";
        parent.is_dir_ = true;
        parent.is_parent_ = true;
        browse_entries_.push_back(parent);
    }

    std::vector<BrowseEntry> dirs;
    std::vector<BrowseEntry> files;

    std::filesystem::directory_iterator it(abs_dir, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }

        const std::filesystem::directory_entry &entry = *it;
        std::error_code ec_type;
        const bool is_dir = entry.is_directory(ec_type);
        if (ec_type)
        {
            continue;
        }

        // sidecar 标签目录不进列表
        if (is_dir && entry.path().filename() == ".tag")
        {
            continue;
        }

        BrowseEntry item;
        item.path_ = entry.path().generic_string();
        item.is_dir_ = is_dir;
        item.name_ = entry.path().filename().u8string();
        if (is_dir)
        {
            item.name_ += "/";
        }

        // 标签取数据库里的记录（未入库的文件标签为空）
        auto info = ts_->getFileInfo(entry.path());
        if (info.has_value())
        {
            item.tags_ = info->tags_;
        }

        if (is_dir)
        {
            dirs.push_back(item);
        }
        else
        {
            files.push_back(item);
        }
    }

    const auto byName = [](const BrowseEntry &a, const BrowseEntry &b) { return a.name_ < b.name_; };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    browse_entries_.insert(browse_entries_.end(), dirs.begin(), dirs.end());
    browse_entries_.insert(browse_entries_.end(), files.begin(), files.end());
    return true;
}

bool TransferData::isRootPath(const std::string &path) const
{
    if (!dm_)
    {
        return false;
    }

    const std::filesystem::path target = std::filesystem::path(path).lexically_normal();
    for (const auto &root : dm_->getValidDirList())
    {
        std::error_code ec;
        const std::filesystem::path abs_root = std::filesystem::absolute(root, ec).lexically_normal();
        if (ec)
        {
            continue;
        }
        if (abs_root == target)
        {
            return true;
        }
    }
    return false;
}
bool TransferData::addDir(const std::string &path)
{
    if (isBlank(path) || !dm_)
    {
        return false;
    }
    const std::filesystem::path p(path);
    if (!dm_->addDirectory(p))
    {
        return false;
    }
    if (ts_)
    {
        ts_->addRoot(dm_->getLastValidDir()); // 用规范化后的有效路径同步数据库索引
    }
    refreshDirList();
    return true;
}

bool TransferData::removeDir(const std::string &path)
{
    if (isBlank(path) || !dm_)
    {
        return false;
    }
    const std::filesystem::path p(path);
    if (!dm_->removeDirectory(p))
    {
        return false;
    }
    if (ts_)
    {
        ts_->removeRoot(p);
    }
    refreshDirList();
    return true;
}

bool TransferData::addTagToList(const std::string &type, const std::string &tag)
{
    if (isBlank(type) || isBlank(tag) || !ts_ || !ts_->addTag(type, tag))
    {
        return false;
    }
    ts_->saveTag();
    refreshTagLibrary();
    return true;
}

bool TransferData::addTypeToList(const std::string &type, const std::string &color)
{
    if (isBlank(type) || !ts_)
    {
        return false;
    }
    std::string c = color;
    if (!ts_->addType(type, c))
    {
        return false;
    }
    ts_->saveTag();
    refreshTagLibrary();
    return true;
}

bool TransferData::setTypeColor(const std::string &type, const std::string &color)
{
    if (isBlank(type) || isBlank(color) || !ts_ || !ts_->setTypeColor(type, color))
    {
        return false;
    }
    ts_->saveTag();
    refreshTagLibrary();
    return true;
}

bool TransferData::removeTag(const std::string &tag)
{
    if (isBlank(tag) || !ts_ || !ts_->removeTag(tag))
    {
        return false;
    }
    ts_->saveTag();
    refreshTagLibrary();
    return true;
}

bool TransferData::removeType(const std::string &type)
{
    if (isBlank(type) || !ts_ || !ts_->removeType(type))
    {
        return false;
    }
    ts_->saveTag();
    refreshTagLibrary();
    return true;
}

// 导出标签库: 把当前 tag.json 复制一份到用户选择的位置(目录自动创建 同名直接覆盖)
bool TransferData::exportTagList(const std::string &dest_path_utf8)
{
    if (isBlank(dest_path_utf8) || !ts_)
    {
        return false;
    }

    const std::filesystem::path src = ts_->getTagPath();
    std::error_code ec;
    if (src.empty() || !std::filesystem::exists(src, ec) || ec)
    {
        return false;
    }

    std::filesystem::path dest(dest_path_utf8);
    if (dest.extension().empty())
    {
        dest += ".json"; // 未写扩展名时补 .json
    }

    if (!dest.parent_path().empty())
    {
        std::filesystem::create_directories(dest.parent_path(), ec);
    }

    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

// 导入标签库: 合并另一个 tag.json(类型/标签全局唯一 只补充本库没有的) 成功后刷新库数据
bool TransferData::importTagList(const std::string &src_path_utf8)
{
    if (isBlank(src_path_utf8) || !ts_)
    {
        return false;
    }

    if (!ts_->mergeTags(std::filesystem::path(src_path_utf8)))
    {
        return false;
    }

    refreshTagLibrary();
    return true;
}

bool TransferData::addTagToFile(const std::string &path, const std::string &tag)
{
    if (isBlank(path) || isBlank(tag) || !ts_ || !ts_->addFileTag(std::filesystem::path(path), tag))
    {
        return false;
    }
    // 数据库同步(filename 模式会改名 -> 删旧行/写新行)由 TagServe 内部完成
    // 这里传原路径: refreshFileTags 会按真实路径解析并清掉旧键
    refreshFileTags(path);
    return true;
}

bool TransferData::removeTagToFile(const std::string &path, const std::string &tag)
{
    if (isBlank(path) || isBlank(tag) || !ts_ || !ts_->removeFileTag(std::filesystem::path(path), tag))
    {
        return false;
    }
    // 数据库同步由 TagServe 内部完成; 传原路径由 refreshFileTags 解析真实路径并清旧键
    refreshFileTags(path);
    return true;
}

bool TransferData::updataFile()
{
    notify();
    return true;
}

bool TransferData::updataDirList()
{
    refreshDirList();
    notify();
    return true;
}

bool TransferData::updataTagList()
{
    refreshTagLibrary();
    notify();
    return true;
}

// 增量更新单个数据给 LibraryTag.qml 控件
bool TransferData::updataAddTag(const std::string &type, const std::string &tag)
{
    if (isBlank(type) || isBlank(tag))
    {
        return false;
    }
    type_tags_[type].push_back(tag);
    notify();
    return true;
}

bool TransferData::updataAddType(const std::string &type, const std::string &color)
{
    if (isBlank(type))
    {
        return false;
    }
    type_tags_[type];
    type_color_[type] = color;
    notify();
    return true;
}

bool TransferData::updataRemoveTag(const std::string &type, const std::string &tag)
{
    if (isBlank(type) || isBlank(tag))
    {
        return false;
    }
    auto it = type_tags_.find(type);
    if (it != type_tags_.end())
    {
        auto &tags = it->second;
        tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    }
    notify();
    return true;
}

bool TransferData::updataRemoveType(const std::string &type, const std::string &color)
{
    (void)color;
    if (isBlank(type))
    {
        return false;
    }
    type_tags_.erase(type);
    type_color_.erase(type);
    notify();
    return true;
}
