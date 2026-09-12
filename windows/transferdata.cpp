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
