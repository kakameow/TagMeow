#include "bridge.h"

#include "log.h"

#include <QTimer>

#include <set>
#include <QDebug>
#include <QQmlProperty>
#include <QMetaObject>
#include <QVariantMap>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QUrl>

Bridge::Bridge(QObject *parent) : QObject(parent), config_(), dm_("./config/path.json"), language_("./language"),
                                  ts_(dm_.getValidDirList(), config_.tag_mode_, "./config/tag.json", "./config/index.db"),
                                  td_(&dm_, &ts_)
{
    if (!language_.loadLanguage(config_.default_language_))
    {
        error_string_ = "[warning] language load failed: " + language_.getLastError();
    }

    // SyncServer/SyncClient 工作线程回调 -> 主线程状态栏
    QObject::connect(this, &Bridge::syncServerTip, this, &Bridge::onServerTipArrived);
    QObject::connect(this, &Bridge::syncClientTip, this, &Bridge::onClientTipArrived);
    // 任务(目录)级完成提示：工作线程发信号 -> 主线程格式化文案
    QObject::connect(this, &Bridge::syncServerTaskDone, this, &Bridge::onServerTaskDone);
    QObject::connect(this, &Bridge::syncClientTaskDone, this, &Bridge::onClientTaskDone);
}

Bridge::~Bridge() = default;

std::string Bridge::getLastError() const
{
    return error_string_;
}

// 日志：截获鼠标/键盘输入 不消费事件 处理完后（下一轮事件循环）收集各类 error_string_
// 记录格式：[时间] 类名: error_string_
bool Bridge::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (event != nullptr && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease))
    {
        QTimer::singleShot(0, this, [this]() { collectLogMessages(); });
    }
    return false;
}

void Bridge::collectLogMessages()
{
    const auto logOne = [this](const std::string &class_name, const std::string &message)
    {
        if (message.empty())
        {
            return; // 空消息跳过
        }

        auto it = last_logged_.find(class_name);
        if (it != last_logged_.end() && it->second == message)
        {
            return; // 与上次相同 不重复写
        }

        last_logged_[class_name] = message;
        log_.write(class_name, message);
    };

    logOne("Bridge", error_string_);
    logOne("ConfigLoader", config_.error_string_);
    logOne("DirectoryConfigManager", dm_.getLastError());
    logOne("LanguageManager", language_.getLastError());
    logOne("TagServe", ts_.getLastError());
    logOne("FileDatabase", ts_.getDBError());
    logOne("TagLibrary", ts_.getTagError());
    logOne("TagFileManager", ts_.getFileError());

    if (s_server_)
    {
        logOne("SyncServer", s_server_->getLastError());
    }
    if (s_client_)
    {
        logOne("SyncClient", s_client_->getLastError());
    }
}

QObject *Bridge::findObject(const char *name) const
{
    return root_ ? root_->findChild<QObject *>(QString::fromLatin1(name)) : nullptr;
}

QString Bridge::textOf(const char *name) const
{
    QObject *o = findObject(name);
    return o ? QQmlProperty(o, "text").read().toString().trimmed() : QString();
}

void Bridge::pushFileList()
{
    if (!fileContainer_)
    {
        return;
    }

    QVariantList list;

    const auto appendRow = [this, &list](const std::string &path, const std::string &name, const QString &kind, const std::vector<std::string> &tagList)
    {
        QVariantMap item;
        item.insert("text", QString::fromStdString(path));
        item.insert("name", QString::fromStdString(name));
        item.insert("kind", kind);
        QVariantList tags;
        for (const auto &t : tagList)
        {
            QVariantMap tag;
            tag.insert("text", QString::fromStdString(t));
            tag.insert("color", QString::fromStdString(ts_.getColorByTag(t)));
            tags << tag;
        }
        item.insert("tags", tags);
        list << item;
    };

    // 目录浏览模式：一行一个直接子项（最上面是"返回上层"入口）
    if (!td_.browse_current_dir_.empty())
    {
        const std::string current = td_.browse_current_dir_;
        td_.browseDir(current);
        for (const auto &entry : td_.browse_entries_)
        {
            const QString kind = entry.is_parent_ ? QStringLiteral("parent") : (entry.is_dir_ ? QStringLiteral("dir") : QStringLiteral("file"));
            appendRow(entry.path_, entry.name_, kind, entry.tags_);
        }

        QQmlProperty(fileContainer_, "fileList").write(list);
        error_string_ = "[tip] FileContainer browse listing: " + current + " rows " + std::to_string(list.size());
        return;
    }

    const auto kindOf = [](const std::string &path)
    {
        const QFileInfo info(QString::fromStdString(path));
        return info.isDir() ? QStringLiteral("dir") : QStringLiteral("file");
    };

    // 搜索结果模式：按 TransferData 维护的显示顺序渲染(filename 模式改名不会打乱行顺序)
    std::set<std::string> pushed;
    for (const auto &path : td_.file_order_)
    {
        auto it = td_.path_tags_.find(path);
        if (it != td_.path_tags_.end())
        {
            appendRow(it->first, "", kindOf(it->first), it->second);
            pushed.insert(it->first);
        }
    }
    // 兜底: 顺序表里没有的键(理论上不会出现)
    for (const auto &kv : td_.path_tags_)
    {
        if (pushed.find(kv.first) == pushed.end())
        {
            appendRow(kv.first, "", kindOf(kv.first), kv.second);
        }
    }

    QQmlProperty(fileContainer_, "fileList").write(list);
    error_string_ = "[tip] FileContainer search result rows: " + std::to_string(list.size());
}

void Bridge::pushDirList()
{
    if (!dirContainer_)
    {
        return;
    }
    QVariantList list;
    for (const auto &p : td_.path_list_)
    {
        QVariantMap item;
        item.insert("text", QString::fromStdString(p));
        list << item;
    }
    QQmlProperty(dirContainer_, "dirList").write(list);
}

void Bridge::pushTagList()
{
    if (!libraryTag_)
    {
        return;
    }
    QVariantList list;
    std::set<std::string> names;
    for (const auto &kv : td_.type_color_)
        names.insert(kv.first);
    for (const auto &kv : td_.type_tags_)
        names.insert(kv.first);
    for (const auto &name : names)
    {
        QVariantMap item;
        item.insert("typeName", QString::fromStdString(name));
        auto it = td_.type_color_.find(name);
        item.insert("color", QString::fromStdString(it != td_.type_color_.end() ? it->second : "#FFB6C1"));
        QVariantList tags;
        auto it2 = td_.type_tags_.find(name);
        if (it2 != td_.type_tags_.end())
        {
            for (const auto &t : it2->second)
            {
                tags << QString::fromStdString(t);
            }
        }
        item.insert("tags", tags);
        list << item;
    }
    QQmlProperty(libraryTag_, "typeList").write(list);
}

void Bridge::doSearch()
{
    std::vector<std::string> include_, exclude_, only_;
    const auto readTags = [this](QObject *container, std::vector<std::string> &out)
    {
        if (!container)
        {
            return;
        }
        const QVariantList list = QQmlProperty(container, "tagList").read().toList();
        for (const QVariant &e : list)
        {
            const QVariantMap m = e.toMap();
            if (m.contains("text"))
            {
                out.push_back(m.value("text").toString().toStdString());
            }
        }
    };
    readTags(includeContainer_, include_);
    readTags(excludeContainer_, exclude_);
    readTags(onlyContainer_, only_);

    td_.setSearchTags(include_, exclude_, only_);
    if (td_.getSearch())
    {
        error_string_ = "[tip] search ok, files: " + std::to_string(td_.path_tags_.size());
        pushFileList();
    }
    else
    {
        error_string_ = "[error] search failed";
    }
}

void Bridge::onSearchClicked()
{
    doSearch();
}

void Bridge::onFileTagAdded(const QString &path, const QString &tag)
{
    if (td_.addTagToFile(path.toStdString(), tag.toStdString()))
    {
        error_string_ = "[tip] file tag added: " + path.toStdString() + " + " + tag.toStdString();
        // 数据库同步(含 filename 模式改名)由 TagServe 完成; 这里原地重绘
        // (refreshFileTags 已把新路径顶替旧路径 -> 行顺序保持不变)
        pushFileList();
    }
    else
    {
        error_string_ = "[error] add file tag failed: " + path.toStdString() + " + " + tag.toStdString();
    }
}

void Bridge::onFileTagRemoved(const QString &path, const QString &tag)
{
    if (td_.removeTagToFile(path.toStdString(), tag.toStdString()))
    {
        error_string_ = "[tip] file tag removed: " + path.toStdString() + " - " + tag.toStdString();
        pushFileList();
    }
    else
    {
        error_string_ = "[error] remove file tag failed: " + path.toStdString() + " - " + tag.toStdString();
    }
}

void Bridge::onFileTagChanged(const QString &path, const QString &oldTag, const QString &newTag)
{
    // filename 模式下删除会改名 -> 用 TagServe 回报的真实路径继续加新标签
    if (td_.removeTagToFile(path.toStdString(), oldTag.toStdString()))
    {
        const QString cur = QString::fromStdString(ts_.getLastFilePath().generic_u8string());
        if (td_.addTagToFile(cur.toStdString(), newTag.toStdString()))
        {
            error_string_ = "[tip] file tag changed: " + path.toStdString() + " -> " + cur.toStdString();
            pushFileList();
            return;
        }
    }
    error_string_ = "[error] change file tag failed: " + path.toStdString();
}

void Bridge::onAddDirClicked()
{
    const QString path = textOf("dirInput");
    if (td_.addDir(path.toStdString()))
    {
        error_string_ = "[tip] directory added: " + path.toStdString();
        pushDirList();
    }
    else
    {
        error_string_ = "[error] add directory failed: " + path.toStdString();
    }
}

void Bridge::onRemoveDirClicked()
{
    const QString path = textOf("dirInput");
    if (td_.removeDir(path.toStdString()))
    {
        error_string_ = "[tip] directory removed: " + path.toStdString();
        pushDirList();
    }
    else
    {
        error_string_ = "[error] remove directory failed: " + path.toStdString();
    }
}

void Bridge::onAddTagClicked()
{
    const QString type = textOf("typeInput");
    const QString tag = textOf("tagInput");
    if (td_.addTagToList(type.toStdString(), tag.toStdString()))
    {
        error_string_ = "[tip] tag added to library: " + type.toStdString() + " / " + tag.toStdString();
        pushTagList();
    }
    else
    {
        error_string_ = "[error] add tag failed: " + type.toStdString() + " / " + tag.toStdString();
    }
}

void Bridge::onAddTypeClicked()
{
    const QString type = textOf("typeInput");
    const QString color = textOf("colorInput");
    if (td_.addTypeToList(type.toStdString(), color.toStdString()))
    {
        error_string_ = "[tip] type added: " + type.toStdString() + " " + color.toStdString();
        pushTagList();
    }
    else
    {
        error_string_ = "[error] add type failed: " + type.toStdString();
    }
}

void Bridge::onClearClicked()
{
    const char *names[] = { "includeContainer", "excludeContainer", "onlyContainer" };
    for (const char *name : names)
    {
        QObject *container = findObject(name);
        if (container)
        {
            QQmlProperty(container, "tagList").write(QVariantList());
            error_string_ = "[tip] cleared: " + std::string(name);
        }
    }
}

void Bridge::onRemoveTagClicked()
{
    const QString tag = textOf("tagInput");
    if (td_.removeTag(tag.toStdString()))
    {
        error_string_ = "[tip] tag removed from library: " + tag.toStdString();
        pushTagList();
    }
    else
    {
        error_string_ = "[error] remove tag failed: " + tag.toStdString();
    }
}

void Bridge::onRemoveTypeClicked()
{
    const QString type = textOf("typeInput");
    if (td_.removeType(type.toStdString()))
    {
        error_string_ = "[tip] type removed: " + type.toStdString();
        pushTagList();
    }
    else
    {
        error_string_ = "[error] remove type failed: " + type.toStdString();
    }
}

// resetTypeColor: typeInput 的类型 + colorInput 的颜色 -> TagServe::setTypeColor
void Bridge::onResetTypeColorClicked()
{
    const QString type = textOf("typeInput");
    const QString color = textOf("colorInput");
    if (td_.setTypeColor(type.toStdString(), color.toStdString()))
    {
        error_string_ = "[tip] type color updated: " + type.toStdString() + " " + color.toStdString();
        pushTagList();
        pushFileList();
    }
    else
    {
        error_string_ = "[error] update type color failed: " + type.toStdString() + " " + color.toStdString() + " " + ts_.getTagError();
    }
}

void Bridge::onDirDoubleClicked(const QString &path)
{
    // 双击目录: 顺带把路径回填到 dirInput 输入框
    if (QObject *input = findObject("dirInput"))
    {
        QQmlProperty(input, "text").write(path);
    }
    enterDir(path);
}

// 进入目录浏览：列该目录一层（不在授权根时最上面是"返回上层"入口）
void Bridge::enterDir(const QString &path)
{
    const std::string dir = path.toStdString();
    if (!td_.browseDir(dir))
    {
        error_string_ = "[warning] cannot enter directory (not inside an authorized root or not a directory): " + path.toStdString();
        return;
    }

    error_string_ = "[tip] browse enter: " + path.toStdString() + " rows " + std::to_string(td_.browse_entries_.size());
    pushFileList();
}

// LibraryTag 类型名行(点击展开/收起): 顺带把类型名回填到 typeInput 输入框
void Bridge::onLibraryTypeClicked(const QString &type)
{
    if (QObject *input = findObject("typeInput"))
    {
        QQmlProperty(input, "text").write(type);
    }
    error_string_ = "[tip] library type row clicked: " + type.toStdString();
}

void Bridge::openInExplorer(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        error_string_ = "[warning] path does not exist: " + path.toStdString();
        return;
    }

    if (info.isDir())
    {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())))
        {
            error_string_ = "[tip] opened directory: " + info.absoluteFilePath().toStdString();
        }
        else
        {
            error_string_ = "[error] open directory failed: " + info.absoluteFilePath().toStdString();
        }
        return;
    }

#ifdef Q_OS_WIN
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(info.absoluteFilePath());
    if (QProcess::startDetached(QStringLiteral("explorer"), args))
    {
        error_string_ = "[tip] selected file in explorer: " + info.absoluteFilePath().toStdString();
    }
    else
    {
        error_string_ = "[error] select file in explorer failed: " + info.absoluteFilePath().toStdString();
    }
#else
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath())))
    {
        error_string_ = "[tip] opened containing directory: " + info.absolutePath().toStdString();
    }
    else
    {
        error_string_ = "[error] open containing directory failed: " + info.absolutePath().toStdString();
    }
#endif
}

// 双击：返回上层入口 -> 上一级 目录 -> 进入该目录 文件 -> 打开文件
void Bridge::onFileDoubleClicked(const QString &path, const QString &kind)
{
    if (kind == QStringLiteral("parent"))
    {
        if (path.isEmpty())
        {
            error_string_ = "[warning] go to parent failed: empty path";
            return;
        }
        enterDir(path);
        return;
    }

    if (kind == QStringLiteral("dir"))
    {
        enterDir(path);
        return;
    }

    const QFileInfo info(path);
    if (!info.exists())
    {
        error_string_ = "[warning] file does not exist: " + path.toStdString();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())))
    {
        error_string_ = "[error] open file failed: " + info.absoluteFilePath().toStdString();
    }
}

// 右键：原双击行为（目录 -> 资源管理器打开；文件 -> 打开所在目录并高亮选中）
void Bridge::onFileRightClicked(const QString &path, const QString &kind)
{
    if (kind == QStringLiteral("parent"))
    {
        return; // "返回上层"入口不响应右键
    }
    openInExplorer(path);
}
// refreshButton: 强制刷新
// 重新校验目录(DirectoryConfigManager 移除失效目录 逐目录 isPathAllowed 校验)
// 重新加载标签库(TagServe::reLoadTag)
// 重新加载数据库根目录(TagServe::reLoadRoot 同步文件系统)
// 前端刷新：DirContainer / LibraryTag（FileContainer 不刷新）
void Bridge::onRefreshClicked()
{
    error_string_ = "[tip] force refresh started";

    dm_.clearInvalidPath();
    dm_.saveToFile();

    if (!ts_.reLoadTag("./config/tag.json"))
    {
        error_string_ = "[error] refresh failed: reload tag library: " + ts_.getTagError();
    }

    if (!ts_.reLoadRoot(dm_.getValidDirList()))
    {
        error_string_ = "[error] refresh failed: reload database roots: " + ts_.getDBError();
    }

    td_.updataDirList();
    td_.updataTagList();
    pushDirList();
    pushTagList();

    error_string_ = "[tip] force refresh done";
}

void Bridge::pushConfig()
{
    if (root_)
    {
        QQmlProperty(root_, "fontSize").write(config_.font_size_);
        QQmlProperty(root_, "theme").write(config_.theme_);
    }
    const auto setText = [this](const char *name, const QString &text)
    {
        if (QObject *o = findObject(name))
        {
            QQmlProperty(o, "text").write(text);
        }
    };

    setText("versionValue", QString::fromStdString(config_.version_));
    setText("portValue", QString::number(config_.broadcast_port_));
    setText("magicValue", QString::fromStdString(config_.broadcast_magic_word_));
    setText("tagModeValue", config_.tag_mode_ == TagFileManager::StoreMode::Filename ? QStringLiteral("Filename") : QStringLiteral("Sidecar"));

    if (QObject *spin = findObject("waitSpin"))
    {
        QQmlProperty(spin, "value").write(static_cast<int>(config_.server_waiting_time_.count()));
    }
    if (QObject *path = findObject("downloadPath"))
    {
        QQmlProperty(path, "text").write(QString::fromStdString(config_.download_path_.string()));
    }
    if (QObject *spin = findObject("fontSizeSetting"))
    {
        QQmlProperty(spin, "value").write(config_.font_size_);
    }
    if (QObject *combo = findObject("themeCombo"))
    {
        QQmlProperty(combo, "currentIndex").write(config_.theme_);
    }
    // 语言列表与当前语言: 由 pushLanguageList 按语言文件目录填充(同 test.cpp loadLanguageList)
}

// 字号: 修改即生效 -> 写 config_ 保存 -> 刷新 window.fontSize(Controls 字体即时级联)
// Qt6 SpinBox 的 valueChanged 为无参信号, 槽内从控件读取当前值
void Bridge::onFontSizeChanged()
{
    QObject *spin = findObject("fontSizeSetting");
    const int size = spin ? QQmlProperty(spin, "value").read().toInt() : 0;
    if (size == config_.font_size_)
    {
        return;
    }
    if (size < 6 || size > 48)
    {
        error_string_ = "[warning] font size out of range: " + std::to_string(size);
        return;
    }
    config_.font_size_ = size;
    config_.saveConfig();
    if (root_)
    {
        QQmlProperty(root_, "fontSize").write(size); // 各控件 font.pixelSize: window.fontSize 绑定即时级联
    }
    error_string_ = "[tip] font size applied: " + std::to_string(size);
}

// 主题: 修改即生效 -> 写 config_ 保存 -> pushTheme 刷新 uiColor 与全局调色板
void Bridge::onThemeChanged(int index)
{
    if (index < 0)
    {
        index = 0;
    }
    config_.theme_ = index;
    config_.saveConfig();
    pushTheme();
    error_string_ = "[tip] theme applied: " + std::to_string(index);
}

// setWindow 确认: 读取控件 -> 保存 config.json -> 退出程序(语言/模式等重启后生效)
void Bridge::onSaveConfigClicked()
{
    if (QObject *spin = findObject("waitSpin"))
    {
        int minutes = QQmlProperty(spin, "value").read().toInt();
        if (minutes >= 0)
        {
            config_.server_waiting_time_ = std::chrono::minutes(minutes);
        }
    }
    if (QObject *path = findObject("downloadPath"))
    {
        const QString s = QQmlProperty(path, "text").read().toString().trimmed();
        if (!s.isEmpty())
        {
            config_.download_path_ = s.toStdString();
        }
    }
    if (QObject *combo = findObject("languageCombo"))
    {
        const QString lang = QQmlProperty(combo, "currentText").read().toString().trimmed();
        if (!lang.isEmpty())
        {
            config_.default_language_ = lang.toStdString();
        }
    }

    if (config_.saveConfig())
    {
        error_string_ = "[tip] config saved: language " + config_.default_language_ + " wait " + std::to_string(config_.server_waiting_time_.count()) + " download " + config_.download_path_.string();
    }
    else
    {
        error_string_ = "[error] save config failed: " + config_.error_string_;
    }

    error_string_ = "[tip] config saved, application will quit (some settings apply after restart)";
    QCoreApplication::quit();
}

void Bridge::onConvertModeConfirmed()
{
    const TagFileManager::StoreMode form = config_.tag_mode_;
    const TagFileManager::StoreMode to = (form == TagFileManager::StoreMode::Sidecar)  ? TagFileManager::StoreMode::Filename : TagFileManager::StoreMode::Sidecar;

    if (ts_.convertMode(form, to))
    {
        config_.tag_mode_ = to;
        config_.saveConfig();
        error_string_ = "[tip] convert mode done -> " + std::string(to == TagFileManager::StoreMode::Filename ? "Filename" : "Sidecar");
        // 数据库重建索引已由 TagServe::convertMode 内部完成(转换会改写文件名)
        pushConfig(); // setWindow 的 TagMode 只读项同步新模式
        doSearch();   // 路径可能已变化 -> 按当前搜索条件刷新文件列表
        pushDirList();
        pushTagList();
    }
    else
    {
        error_string_ = "[error] convert mode failed: " + ts_.getLastError() + " / " + ts_.getFileError();
    }
}

// 导出标签库: FileDialog 选好的目标位置(file:// URL) -> 复制 tag.json
void Bridge::onExportFileChosen(const QString &url)
{
    const QString path = QUrl(url).toLocalFile();
    if (path.isEmpty())
    {
        error_string_ = "[warning] export failed: invalid path: " + url.toStdString();
        return;
    }

    if (td_.exportTagList(path.toStdString()))
    {
        error_string_ = "[tip] export done -> " + path.toStdString();
    }
    else
    {
        error_string_ = "[error] export failed -> " + path.toStdString();
    }
}

// 导入标签库: FileDialog 选好的文件(file:// URL) -> TagServe::mergeTags 合并并落盘
void Bridge::onImportFileChosen(const QString &url)
{
    const QString path = QUrl(url).toLocalFile();
    if (path.isEmpty())
    {
        error_string_ = "[warning] import failed: invalid path: " + url.toStdString();
        return;
    }

    if (td_.importTagList(path.toStdString()))
    {
        error_string_ = "[tip] import done -> " + path.toStdString();
        pushTagList();  // 标签库变化
        pushFileList(); // 标签颜色可能变化 -> 文件行重绘
    }
    else
    {
        error_string_ = "[error] import failed -> " + path.toStdString() + " " + ts_.getTagError();
    }
}

void Bridge::setStatusLabel(const char *name, const QString &text)
{
    if (QObject *o = findObject(name))
    {
        QQmlProperty(o, "text").write(text);
    }
}

// UI 文案表: id -> 中文默认值(内置兜底, 语言文件缺失时界面仍为中文)
// 对应语言文件: language/*.json 的 text 数组(id/str) 由 CMake 构建后复制到运行目录
namespace
{
struct TextItem
{
    const char *id;
    const char *zh;
};
const TextItem kUiTextTable[] = {
    // 主界面标题栏/按钮
    { "bar.settings", "设置" },
    { "bar.options", "存储模式设置" },
    { "bar.help", "帮助" },
    { "tip.file", "文件:" },
    { "tip.tag", "标签:" },
    // 搜索容器
    { "cnt.include", "包含" },
    { "cnt.exclude", "排除" },
    { "cnt.only", "只有" },
    { "cnt.tip", "拖拽标签到这里" },
    { "btn.search", "按标签搜索" },
    { "btn.clear", "清空搜索框的标签" },
    { "btn.refresh", "强制刷新数据同步磁盘\n此操作可能耗时" },
    { "btn.addDir", "添加目录" },
    { "btn.addType", "添加类型" },
    { "btn.addTag", "添加标签" },
    { "btn.removeDir", "删除目录" },
    { "btn.removeType", "删除类型" },
    { "btn.removeTag", "删除标签" },
    { "btn.resetColor", "更新类型颜色" },
    { "btn.syncWindow", "打开局域网文件同步界面" },
    { "btn.export", "导出标签库"},
    { "btn.import", "导入标签库"},
    { "settings.version", "版本:" },
    { "settings.port", "广播端口:" },
    { "settings.magic", "魔术字:" },
    { "settings.tagMode", "标签模式:" },
    { "settings.wait", "等待时间(分钟):" },
    { "settings.download", "默认下载路径:" },
    { "settings.language", "语言:" },
    { "settings.fontSize", "字号:" },
    { "settings.theme", "主题:" },
    { "settings.tags", "(类型标签全局唯一 合并可能改变分类):"},
    { "theme.light", "浅色" },
    { "theme.dark", "深色" },
    { "settings.restartTip", "某些配置可能需要重启后生效点击确认保存并关闭程序" }, // 原为带换行的版本，这里合并为一行（但 JSON 中是单行，去掉了换行）
    { "btn.ok", "确认" },
    { "btn.cancel", "取消" },
    // 转换模式对话框
    { "options.title", "是否转换默认存储模式\n(额外的json文件存储 <-> 在文件名字后面存储)\n此操作可能费时可能需要重启程序或者刷新显示" },
    // 同步窗口
    { "sync.share", "从本机分享文件" },
    { "sync.download", "从其他设备下载" },
    { "sync.serverName", "输入服务器名称" },
    { "sync.start", "启动" },
    { "sync.stop", "关闭" },
    { "sync.dirPath", "输入目录路径" },
    { "sync.addDir", "添加目录到传输列表" },
    { "sync.disconnect", "断开连接设备" },
    { "sync.back", "返回" },
    { "sync.scan", "搜索局域网设备" },
    { "sync.downloadSel", "下载选中设备" },
    { "sync.clearRecords", "清除下载记录缓存" },
    // Bridge 动态状态栏文本(含 %1 占位符)
    { "status.server.start", "服务器已启动 (%1)" },
    { "status.server.startFail", "启动失败: %1" },
    { "status.server.stop", "服务器已停止" },
    { "status.server.stopFail", "停止失败: %1" },
    { "status.server.dirEmpty", "目录为空" },
    { "status.server.invalidDir", "无效目录: %1" },
    { "status.server.enqueued", "已入队: %1" },
    { "status.server.disconnected", "已断开连接设备" },
    { "status.server.queueEmpty", "服务器: 队列为空 等待目录..." },
    { "status.server.sessionEnd", "服务器: 会话结束" },
    { "status.server.sessionErr", "服务器: 会话出错/中断" },
    { "status.client.scanning", "正在扫描局域网..." },
    { "status.client.scanDone", "扫描完成 %1 台设备" },
    { "status.client.noSelect", "请先扫描并选中一台设备" },
    { "status.client.downloading", "正在下载 %1 ..." },
    { "status.client.queueEmpty", "客户端: 服务器队列为空 本次无文件" },
    { "status.client.done", "客户端: 下载完成" },
    { "status.client.failed", "客户端: 下载失败/断开" },
    { "status.client.cleared", "已清除下载记录" },
    { "status.client.disconnected", "已断开连接" },
    // 任务(一个入队目录)完成提示：%1 任务名 %2 文件数 %3 大小
    { "status.server.taskDone", "服务器: 任务完成 %1 (%2 个文件, %3)" },
    { "status.client.taskDone", "客户端: 任务完成 %1 (%2 个文件, %3)" },
    { "status.client.cacheHint", "（如果没有失败重下的需要 请点击 [清除下载记录缓存]）" },
};

// 字节数 -> 便于阅读的大小文本（任务完成提示用）
QString formatBytes(qulonglong bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0)
    {
        return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
}
} // namespace

QString Bridge::uiText(const char *id, const char *zh) const
{
    const std::string &s = language_.getString(id);
    // LanguageManager 未加载到该 id 时返回 "MISSING_STRING"(缺失哨兵) -> 回退中文
    if (s.empty() || s == "MISSING_STRING")
    {
        return QString::fromUtf8(zh);
    }
    return QString::fromStdString(s);
}

// language_ -> Main.qml window.uiText 字典(QML 绑定 window.uiText["id"])
void Bridge::pushUiText()
{
    if (!root_)
    {
        return;
    }
    QVariantMap map;
    for (const auto &item : kUiTextTable)
    {
        map.insert(QString::fromUtf8(item.id), uiText(item.id, item.zh));
    }
    QQmlProperty(root_, "uiText").write(map);
}

// 双主题配色表: 每个颜色项 key -> {浅色, 深色}
namespace
{
struct ColorItem
{
    const char *key;
    const char *light;
    const char *dark;
};
const ColorItem kColorTable[] = {
    // 背景/边框
    { "page", "#f0f2f5", "#1e2127" },
    { "card", "#ffffff", "#262a31" },
    { "bar", "#f8f9fc", "#23272e" },
    { "border", "#e2e6ee", "#3a4150" },
    // 文字
    { "textMain", "#1a202c", "#e8eaee" },
    { "textSub", "#4a5568", "#aeb4bf" },
    { "textHint", "#a0aec0", "#7c8490" },
    // 标签
    { "tagBg", "#ffffff", "#2e333b" },
    { "tagText", "#2d3748", "#dfe3e9" },
    { "tagBorder", "#e2e6ee", "#3a4150" },
    { "tagHover", "#e2e8f0", "#383e47" },
    // 包含/排除/只有 区
    { "includeBorder", "#48bb78", "#48bb78" },
    { "includeBg", "#f0fff4", "#1d2f24" },
    { "excludeBorder", "#fc8181", "#fc8181" },
    { "excludeBg", "#fff5f5", "#382327" },
    { "onlyBorder", "#63b3ed", "#63b3ed" },
    { "onlyBg", "#ebf8ff", "#1e2c38" },
    // 按钮
    { "btnBg", "#edf2f7", "#2e333b" },
    { "btnText", "#2d3748", "#dfe3e9" },
    { "btnHover", "#e2e8f0", "#383e47" },
    { "primary", "#4a6fa5", "#4a6fa5" },
    { "primaryText", "#ffffff", "#ffffff" },
    { "primaryHover", "#3b5d8a", "#3b5d8a" },
    // 输入框
    { "inputBg", "#ffffff", "#262a31" },
    { "inputBorder", "#e2e6ee", "#3a4150" },
    { "inputFocus", "#4a6fa5", "#4a6fa5" },
    // 行/状态
    { "dirHover", "#edf2f7", "#2e333b" },
    { "rowHover", "#ffffff", "#2b3038" },
    { "statusBar", "#f8f9fc", "#23272e" },
    { "statusText", "#718096", "#98a0ab" },
    // 功能色
    { "danger", "#fc8181", "#fc8181" },
    { "dangerHover", "#f56565", "#f56565" },
    { "icon", "#718096", "#98a0ab" },
    { "iconAccent", "#4a6fa5", "#7fa3d4" },
};
} // namespace

// config_.theme_ -> window.uiColor(当前主题配色表)/themeNames(主题名)/theme 并同步全局调色板
void Bridge::pushTheme()
{
    if (!root_)
    {
        return;
    }
    const int idx = config_.theme_ > 0 ? 1 : 0;

    QVariantMap map;
    for (const auto &c : kColorTable)
    {
        map.insert(QString::fromUtf8(c.key), QString::fromLatin1(idx ? c.dark : c.light));
    }
    QQmlProperty(root_, "uiColor").write(map);
    QQmlProperty(root_, "theme").write(idx);

    // 主题名(本地化) 供 themeCombo 使用
    QVariantList themeNames;
    themeNames << uiText("theme.light", "浅色") << uiText("theme.dark", "深色");
    QQmlProperty(root_, "themeNames").write(themeNames);

    // 全局调色板: Controls 文字/底色跟随主题(浅色文字≈黑; 深色文字变浅)
    const auto pick = [&map](const char *key, const char *dflt) -> QString
    {
        const QString k = QString::fromUtf8(key);
        return map.contains(k) ? map.value(k).toString() : QString::fromLatin1(dflt);
    };
    QPalette pal = QGuiApplication::palette();
    pal.setColor(QPalette::Window, QColor(pick("card", "#ffffff")));
    pal.setColor(QPalette::WindowText, QColor(pick("textMain", "#1a202c")));
    pal.setColor(QPalette::Base, QColor(pick("inputBg", "#ffffff")));
    pal.setColor(QPalette::Text, QColor(pick("textMain", "#1a202c")));
    pal.setColor(QPalette::Button, QColor(pick("btnBg", "#edf2f7")));
    pal.setColor(QPalette::ButtonText, QColor(pick("btnText", "#2d3748")));
    pal.setColor(QPalette::PlaceholderText, QColor(pick("textHint", "#a0aec0")));
    pal.setColor(QPalette::ToolTipBase, QColor(pick("card", "#ffffff")));
    pal.setColor(QPalette::ToolTipText, QColor(pick("textMain", "#1a202c")));
    QGuiApplication::setPalette(pal);

    error_string_ = "[tip] theme pushed, index: " + std::to_string(idx);
}

void Bridge::pushLanguageList()
{
    if (!root_)
    {
        return;
    }
    const auto &langs = language_.getLanguagesList();
    QVariantList names;
    for (const auto &n : langs)
    {
        names << QString::fromStdString(n);
    }
    QQmlProperty(root_, "languageNames").write(names);

    if (QObject *combo = findObject("languageCombo"))
    {
        int idx = 0;
        for (size_t i = 0; i < langs.size(); i++)
        {
            if (langs[i] == config_.default_language_)
            {
                idx = static_cast<int>(i);
                break;
            }
        }
        QQmlProperty(combo, "currentIndex").write(idx);
    }
}

void Bridge::pushServerQueue()
{
    QObject *w = findObject("syncWindow");
    if (!w)
    {
        error_string_ = "[warning] syncWindow not found: server queue not pushed";
        return;
    }
    QVariantList list;
    const auto queue = s_server_->getTaskQueue();
    for (const auto &p : queue)
    {
        list << QString::fromStdString(p.generic_u8string());
    }
    QQmlProperty(w, "serverQueue").write(list);
}

void Bridge::onServerStartClicked()
{
    std::string name = textOf("serverNameField").toStdString();
    if (name.empty())
    {
        name = "tagmeow";
    }
    std::error_code ec;
    const bool ok = s_server_->start(name, 0, ec, [this](bool success, std::error_code e)
    {
        QString msg;
        if (success)
        {
            msg = (e == std::make_error_code(std::errc::no_message_available)) ? uiText("status.server.queueEmpty", "服务器: 队列为空 等待目录...") : uiText("status.server.sessionEnd", "服务器: 会话结束");
        }
        else
        {
            msg = uiText("status.server.sessionErr", "服务器: 会话出错/中断");
        }
        emit syncServerTip(msg); // 工作线程回调 信号自动排队到主线程
    });
    if (ok)
    {
        error_string_ = "[tip] server started, name: " + name;
        setStatusLabel("serverStatusLabel", uiText("status.server.start", "服务器已启动 (%1)").arg(QString::fromStdString(name)));
    }
    else
    {
        error_string_ = "[error] server start failed: " + ec.message();
        setStatusLabel("serverStatusLabel", uiText("status.server.startFail", "启动失败: %1").arg(QString::fromStdString(ec.message())));
    }
}

void Bridge::onServerStopClicked()
{
    std::error_code ec;
    s_server_->stop(ec);
    if (!ec)
    {
        error_string_ = "[tip] server stopped";
        setStatusLabel("serverStatusLabel", uiText("status.server.stop", "服务器已停止"));
    }
    else
    {
        error_string_ = "[error] server stop failed: " + ec.message();
        setStatusLabel("serverStatusLabel", uiText("status.server.stopFail", "停止失败: %1").arg(QString::fromStdString(ec.message())));
    }
}

void Bridge::onServerAddDirClicked()
{
    const QString dir = textOf("serverDirField");
    if (dir.isEmpty())
    {
        setStatusLabel("serverStatusLabel", uiText("status.server.dirEmpty", "目录为空"));
        return;
    }

    std::error_code ec;
    std::filesystem::path p(dir.toStdString());
    const auto abs = std::filesystem::absolute(p, ec);
    if (ec || !std::filesystem::is_directory(abs, ec) || ec)
    {
        error_string_ = "[warning] server enqueue failed: invalid directory: " + dir.toStdString();
        setStatusLabel("serverStatusLabel", uiText("status.server.invalidDir", "无效目录: %1").arg(dir));
        return;
    }

    const auto norm = abs.lexically_normal();
    s_server_->enqueueDirectory(norm);
    error_string_ = "[tip] server enqueued: " + norm.generic_u8string();
    setStatusLabel("serverStatusLabel", uiText("status.server.enqueued", "已入队: %1").arg(QString::fromStdString(norm.generic_u8string())));
    pushServerQueue();
}

void Bridge::onServerDisconnectClicked()
{
    std::error_code ec;
    s_server_->disconnect(ec);
    if (!ec)
    {
        error_string_ = "[tip] server disconnected";
        setStatusLabel("serverStatusLabel", uiText("status.server.disconnected", "已断开连接设备"));
    }
    else
    {
        error_string_ = "[error] server disconnect failed: " + ec.message();
    }
}

void Bridge::onServerTipArrived(const QString &msg)
{
    error_string_ = "[tip] " + msg.toStdString();
    setStatusLabel("serverStatusLabel", msg);
}

void Bridge::pushServerList()
{
    QObject *w = findObject("syncWindow");
    if (!w)
    {
        error_string_ = "[warning] syncWindow not found: server list not pushed";
        return;
    }
    QVariantList list;
    for (const auto &s : s_client_->getServers()) // 线程安全
    {
        QVariantMap m;
        m.insert("name", QString::fromStdString(s.name_));
        m.insert("ip", QString::fromStdString(s.ip_));
        m.insert("port", static_cast<int>(s.port_));
        list << m;
    }
    QQmlProperty(w, "serverList").write(list);
}

void Bridge::onClientScanClicked()
{
    setStatusLabel("clientStatusLabel", uiText("status.client.scanning", "正在扫描局域网..."));
    const auto servers = s_client_->scanServers();
    pushServerList();
    error_string_ = "[tip] client scan done, servers: " + std::to_string(servers.size());
    setStatusLabel("clientStatusLabel", uiText("status.client.scanDone", "扫描完成 %1 台设备").arg(static_cast<int>(servers.size())));
}

void Bridge::onClientDownloadClicked()
{
    QObject *lv = findObject("serverListView");
    const int index = lv ? QQmlProperty(lv, "currentIndex").read().toInt() : -1;
    const auto servers = s_client_->getServers();
    if (index < 0 || static_cast<size_t>(index) >= servers.size())
    {
        error_string_ = "[warning] client download failed: no valid device selected, index " + std::to_string(index);
        setStatusLabel("clientStatusLabel", uiText("status.client.noSelect", "请先扫描并选中一台设备"));
        return;
    }
    const size_t idx = static_cast<size_t>(index);
    error_string_ = "[tip] client download started, index " + std::to_string(index) + " server " + servers[idx].name_;
    setStatusLabel("clientStatusLabel", uiText("status.client.downloading", "正在下载 %1 ...").arg(QString::fromStdString(servers[idx].name_)));
    s_client_->startDownload(idx, [this](bool success, std::error_code e)
    {
        QString msg;
        if (success)
        {
            msg = (e == std::make_error_code(std::errc::no_message_available))
                      ? uiText("status.client.queueEmpty", "客户端: 服务器队列为空 本次无文件")
                      : uiText("status.client.done", "客户端: 下载完成");
        }
        else
        {
            // 具体失败原因（哪个文件、收了多少 / 共多少 错误码）由核心层 getLastError 提供
            msg = uiText("status.client.failed", "客户端: 下载失败/断开");
            const QString detail = QString::fromStdString(s_client_->getLastError()).trimmed();
            if (!detail.isEmpty())
            {
                msg += QStringLiteral(" - ") + detail;
            }
        }
        emit syncClientTip(msg);
    });
}

void Bridge::onClientClearClicked()
{
    s_client_->clearDownloadRecords();
    error_string_ = "[tip] client download records cleared";
    setStatusLabel("clientStatusLabel", uiText("status.client.cleared", "已清除下载记录"));
}

void Bridge::onClientDisconnectClicked()
{
    s_client_->disconnect();
    error_string_ = "[tip] client disconnected";
    setStatusLabel("clientStatusLabel", uiText("status.client.disconnected", "已断开连接"));
}

void Bridge::onClientTipArrived(const QString &msg)
{
    error_string_ = "[tip] " + msg.toStdString();
    setStatusLabel("clientStatusLabel", msg);
}

// 任务(目录)级完成提示：工作线程只传数据 这里在主线程格式化后写状态栏
void Bridge::onServerTaskDone(const QString &name, int fileCount, qulonglong byteCount)
{
    const QString msg = uiText("status.server.taskDone", "服务器: 任务完成 %1 (%2 个文件, %3)")
                            .arg(name)
                            .arg(fileCount)
                            .arg(formatBytes(byteCount));
    error_string_ = "[tip] " + msg.toStdString();
    setStatusLabel("serverStatusLabel", msg);
}

void Bridge::onClientTaskDone(const QString &name, int fileCount, qulonglong byteCount)
{
    const QString msg = uiText("status.client.taskDone", "客户端: 任务完成 %1 (%2 个文件, %3)")
                            .arg(name)
                            .arg(fileCount)
                            .arg(formatBytes(byteCount));
    error_string_ = "[tip] " + msg.toStdString();
    setStatusLabel("clientStatusLabel", msg);
}

// 首次点击 snycWindow(打开同步窗口)时惰性创建 SyncServer/SyncClient
// 参数与 cli/test.cpp 一致(config_ 的值); 只创建一次, 重复点击不再重建
void Bridge::onSyncWindowOpened()
{
    if (!s_server_)
    {
        s_server_ = std::make_unique<SyncServer>(config_.broadcast_port_, config_.broadcast_magic_word_, config_.server_waiting_time_);
        // 任务(入队目录)发完 -> 主线程状态栏提示（工作线程只发信号 不碰 QML）
        s_server_->setTaskCallback([this](const TaskReport &report)
        {
            emit syncServerTaskDone(QString::fromStdString(report.name_), static_cast<int>(report.file_count_),
                                    static_cast<qulonglong>(report.byte_count_));
        });
        error_string_ = "[tip] SyncServer created (first open of the sync window)";
    }
    if (!s_client_)
    {
        s_client_ = std::make_unique<SyncClient>(config_.broadcast_port_, config_.broadcast_magic_word_, config_.download_path_);
        // 任务(入队目录)收完 -> 主线程状态栏提示（工作线程只发信号 不碰 QML）
        s_client_->setTaskCallback([this](const TaskReport &report)
        {
            emit syncClientTaskDone(QString::fromStdString(report.name_), static_cast<int>(report.file_count_),
                                    static_cast<qulonglong>(report.byte_count_));
        });
        error_string_ = "[tip] SyncClient created (first open of the sync window)";
        // 客户端状态栏默认提示：下载记录缓存只增不减 需要重下时由用户手动清理
        setStatusLabel("clientStatusLabel", uiText("status.client.cacheHint", "（如果没有失败重下的需要 请点击 [清除下载记录缓存]）"));
    }
}

void Bridge::bindTo(QObject *root)
{
    root_ = root;
    // 输入流截获：鼠标/键盘事件处理完后收集各类 error_string_ 写日志
    qApp->installEventFilter(this);
    fileContainer_ = findObject("fileContainer");
    dirContainer_ = findObject("dirContainer");
    libraryTag_ = findObject("libraryTag");
    includeContainer_ = findObject("includeContainer");
    excludeContainer_ = findObject("excludeContainer");
    onlyContainer_ = findObject("onlyContainer");

    error_string_ = "[tip] bridge controls bound: fileContainer " + std::string(fileContainer_ ? "ok" : "-") + " dirContainer " + std::string(dirContainer_ ? "ok" : "-") + " libraryTag " + std::string(libraryTag_ ? "ok" : "-") + " searchButton " + std::string(findObject("searchButton") ? "ok" : "-");

    // 搜索按钮
    if (QObject *btn = findObject("searchButton"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onSearchClicked()));
    }
    // 强制刷新按钮
    if (QObject *btn = findObject("refreshButton"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
    }
    // 清空(包含/排除/只有)按钮
    if (QObject *btn = findObject("clearButton"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onClearClicked()));
    }

    // FileContainer 行内标签 添加/删除/修改 + 双击行打开目录/定位文件
    if (fileContainer_)
    {
        QObject::connect(fileContainer_, SIGNAL(fileTagAdded(QString, QString)), this, SLOT(onFileTagAdded(QString, QString)));
        QObject::connect(fileContainer_, SIGNAL(fileTagRemoved(QString, QString)), this, SLOT(onFileTagRemoved(QString, QString)));
        QObject::connect(fileContainer_, SIGNAL(fileTagChanged(QString, QString, QString)), this, SLOT(onFileTagChanged(QString, QString, QString)));
        QObject::connect(fileContainer_, SIGNAL(fileDoubleClicked(QString, QString)), this, SLOT(onFileDoubleClicked(QString, QString)));
        QObject::connect(fileContainer_, SIGNAL(fileRightClicked(QString, QString)), this, SLOT(onFileRightClicked(QString, QString)));
    }

    // DirContainer: 双击目录 -> getDirFile -> 刷新 FileContainer
    if (dirContainer_)
    {
        QObject::connect(dirContainer_, SIGNAL(dirDoubleClicked(QString)), this, SLOT(onDirDoubleClicked(QString)));
    }
    // LibraryTag: 点击类型名行(展开/收起) -> 回填 typeInput
    if (libraryTag_)
    {
        QObject::connect(libraryTag_, SIGNAL(typeHeaderClicked(QString)), this, SLOT(onLibraryTypeClicked(QString)));
    }

    // optionsBar 操作按钮
    if (QObject *btn = findObject("addDirBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onAddDirClicked()));
    }
    if (QObject *btn = findObject("addPathBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onAddDirClicked()));
    }
    if (QObject *btn = findObject("removeDirBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onRemoveDirClicked()));
    }
    if (QObject *btn = findObject("addTagBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onAddTagClicked()));
    }
    if (QObject *btn = findObject("addTypeBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onAddTypeClicked()));
    }
    if (QObject *btn = findObject("removeTagBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onRemoveTagClicked()));
    }
    if (QObject *btn = findObject("removeTypeBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onRemoveTypeClicked()));
    }
    if (QObject *btn = findObject("resetTypeColor"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onResetTypeColorClicked()));
    }
    // setWindow 确认按钮: 保存配置并退出程序
    if (QObject *btn = findObject("saveConfigBtn"))
    {
        QObject::connect(btn, SIGNAL(clicked()), this, SLOT(onSaveConfigClicked()));
    }
    // optionsDialog 确认(Yes)
    if (QObject *dlg = findObject("optionsDialog"))
    {
        QObject::connect(dlg, SIGNAL(accepted()), this, SLOT(onConvertModeConfirmed()));
    }
    // syncWindow 服务器(rect2)
    if (QObject *b = findObject("serverStartBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onServerStartClicked()));
    }
    if (QObject *b = findObject("serverStopBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onServerStopClicked()));
    }
    if (QObject *b = findObject("serverAddDirBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onServerAddDirClicked()));
    }
    if (QObject *b = findObject("serverDisconnectBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onServerDisconnectClicked()));
    }
    // syncWindow 客户端(rect3)
    if (QObject *b = findObject("clientScanBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onClientScanClicked()));
    }
    if (QObject *b = findObject("clientDownloadBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onClientDownloadClicked()));
    }
    if (QObject *b = findObject("clientClearBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onClientClearClicked()));
    }
    if (QObject *b = findObject("clientDisconnectBtn"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onClientDisconnectClicked()));
    }
    // snycWindow 按钮: 首次点击时创建 SyncServer/SyncClient(窗口显示由 QML onClicked 负责)
    if (QObject *b = findObject("snycWindow"))
    {
        QObject::connect(b, SIGNAL(clicked()), this, SLOT(onSyncWindowOpened()));
    }
    // 设置窗口: 字号/主题 修改即生效
    if (QObject *spin = findObject("fontSizeSetting"))
    {
        // Qt6 SpinBox: valueChanged 为无参信号
        QObject::connect(spin, SIGNAL(valueChanged()), this, SLOT(onFontSizeChanged()));
    }
    if (QObject *combo = findObject("themeCombo"))
    {
        QObject::connect(combo, SIGNAL(activated(int)), this, SLOT(onThemeChanged(int)));
    }
    // 设置窗口 标签库 导出/导入: QML FileDialog 选中后经 window 信号回传路径
    if (root_)
    {
        QObject::connect(root_, SIGNAL(exportFileChosen(QString)), this, SLOT(onExportFileChosen(QString)));
        QObject::connect(root_, SIGNAL(importFileChosen(QString)), this, SLOT(onImportFileChosen(QString)));
    }

    // 初始填充（后端磁盘数据 -> 控件 + 语言字典/主题 -> window）
    td_.updataDirList();
    td_.updataTagList();
    pushLanguageList();
    pushUiText();
    pushTheme(); // uiColor/themeNames/全局调色板(themeNames 供 pushConfig 设主题选中项)
    pushConfig();
    pushDirList();
    pushTagList();
}
