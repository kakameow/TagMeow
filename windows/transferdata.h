#ifndef TRANSFERDATA_H
#define TRANSFERDATA_H

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "directory_manager.h"
#include "tag_serve.h"

// TransferData：QML 控件与(core)之间的数据桥
// main 创建 DirectoryConfigManager / TagServe 后经构造或 setBackend 注入（本类不持有指针 生命周期由 main 负责）
// 后端交互方法把结果落在 *_ 成员上：path_list_(目录) type_tags_/type_color_(标签库) path_tags_(文件->标签)
// updata* 方法触发 on_data_changed_ 回调 由 main 通知对应 QML 控件刷新渲染

class TransferData
{
public:
    TransferData(DirectoryConfigManager *dm = nullptr, TagServe *ts = nullptr);
    ~TransferData();

    void setBackend(DirectoryConfigManager *dm, TagServe *ts);

    // 数据变化通知回调
    void setOnDataChanged(std::function<void()> cb);

    // 设置搜索条件：include(包含) exclude(排除) only(只有) 三个容器内的标签集合
    void setSearchTags(const std::vector<std::string> &include, const std::vector<std::string> &exclude, const std::vector<std::string> &only);

    // 按搜索条件提交给后端搜索 将返回结果填充 path_tags_
    bool getSearch();
    // 将路径拿到后端判断是否在沙盒内 如果在取该目录下文件数据填充 path_tags_
    bool getDirFile(const std::string &path);

    // 将目录提交给后端处理 更新 path_list_
    bool addDir(const std::string &path);
    bool removeDir(const std::string &path);

    // 标签库管理（type_tags_ / type_color_）
    bool addTagToList(const std::string &type, const std::string &tag);
    bool addTypeToList(const std::string &type, const std::string &color);
    bool setTypeColor(const std::string &type, const std::string &color);
    bool removeTag(const std::string &tag);
    bool removeType(const std::string &type);

    // 文件标签（path_tags_）
    bool addTagToFile(const std::string &path, const std::string &tag);
    bool removeTagToFile(const std::string &path, const std::string &tag);

    // 通知控件刷新（触发 on_data_changed_）
    bool updataFile();                                    // FileContainer.qml 用 path_tags_
    bool updataDirList();                                 // DirContainer.qml 用 path_list_
    bool updataTagList();                                 // LibraryTag.qml 用 type_tags_/type_color_
    bool updataAddTag(const std::string &type, const std::string &tag);
    bool updataAddType(const std::string &type, const std::string &color);
    bool updataRemoveTag(const std::string &type, const std::string &tag);
    bool updataRemoveType(const std::string &type, const std::string &color);

    // 供 QML(经 main 桥接)读取的数据
    std::vector<std::string> path_list_;
    std::unordered_map<std::string, std::vector<std::string>> type_tags_;
    std::unordered_map<std::string, std::string> type_color_;
    std::unordered_map<std::string, std::vector<std::string>> path_tags_;
    // path_tags_ 的显示顺序(filename 模式改名时原位顶替 保证 UI 行顺序稳定)
    std::vector<std::string> file_order_;

private:
    void refreshDirList();     // 从 DirectoryConfigManager 刷新 path_list_
    void refreshTagLibrary();  // 从 TagServe 刷新 type_tags_/type_color_
    void refreshFileTags(const std::string &path); // 从 TagServe 刷新 path_tags_[path]
    void notify();             // 触发 on_data_changed_

    // 不持有指针 生命周期由 main 负责
    DirectoryConfigManager *dm_;
    TagServe *ts_;
    std::function<void()> on_data_changed_;

    std::vector<std::string> include_tags_;
    std::vector<std::string> exclude_tags_;
    std::vector<std::string> only_tags_;
};

#endif // TRANSFERDATA_H
