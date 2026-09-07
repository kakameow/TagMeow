#include "bridge.h"

#include <set>
#include <QDebug>
#include <QQmlProperty>
#include <QMetaObject>
#include <QVariantMap>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

Bridge::Bridge(QObject *parent) : QObject(parent), config_(), dm_("./config/path.json"), language_("./language"),
                                  ts_(dm_.getValidDirList(), config_.tag_mode_, "./config/tag.json", "./config/index.db"),
                                  s_server_(config_.broadcast_port_, config_.broadcast_magic_word_, config_.server_waiting_time_),
                                  s_client_(config_.broadcast_port_, config_.broadcast_magic_word_, config_.download_path_), td_(&dm_, &ts_)
{
    if (!language_.loadLanguage(config_.default_language_))
    {
        qWarning() << "[language] loadLanguage failed:" << QString::fromStdString(language_.getLastError());
    }

    // SyncServer/SyncClient 工作线程回调 -> 主线程状态栏(跨线程自动排队)
    QObject::connect(this, &Bridge::syncServerTip, this, &Bridge::onServerTipArrived);
    QObject::connect(this, &Bridge::syncClientTip, this, &Bridge::onClientTipArrived);
}

Bridge::~Bridge() = default;

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
    for (const auto &kv : td_.path_tags_)
    {
        QVariantMap item;
        item.insert("text", QString::fromStdString(kv.first));
        QVariantList tags;
        for (const auto &t : kv.second)
        {
            QVariantMap tag;
            tag.insert("text", QString::fromStdString(t));
            // 从标签库解析该标签所属类型的颜色 库里没有则留空(控件用默认色)
            tag.insert("color", QString::fromStdString(ts_.getColorByTag(t)));
            tags << tag;
        }
        item.insert("tags", tags);
        list << item;
    }
    QQmlProperty(fileContainer_, "fileList").write(list);
    qInfo() << "[refresh] FileContainer fileList:" << list.size();
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
    // 以 type_color_ 的键为基准遍历（类型以颜色确认存在） 并集 type_tags_ 取标签 保证 0 标签的空类型也能显示在 LibraryTag
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
        qInfo() << "[search] ok files:" << td_.path_tags_.size();
        pushFileList();
    }
    else
    {
        qWarning() << "[search] failed";
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
        qInfo() << "[fileTagAdded] ok" << path << tag;
        //pushFileList();
    }
    else
    {
        qWarning() << "[fileTagAdded] failed" << path << tag;
    }
}

void Bridge::onFileTagRemoved(const QString &path, const QString &tag)
{
    if (td_.removeTagToFile(path.toStdString(), tag.toStdString()))
    {
        qInfo() << "[fileTagRemoved] ok" << path << tag;
        //pushFileList();
    }
    else
    {
        qWarning() << "[fileTagRemoved] failed" << path << tag;
    }
}

void Bridge::onFileTagChanged(const QString &path, const QString &oldTag, const QString &newTag)
{
    if (td_.removeTagToFile(path.toStdString(), oldTag.toStdString()) && td_.addTagToFile(path.toStdString(), newTag.toStdString()))
    {
        qInfo() << "[fileTagChanged] ok" << path;
        pushFileList();
    }
    else
    {
        qWarning() << "[fileTagChanged] failed" << path;
    }
}

void Bridge::onAddDirClicked()
{
    const QString path = textOf("dirInput");
    if (td_.addDir(path.toStdString()))
    {
        qInfo() << "[addDir] ok" << path;
        pushDirList();
    }
    else
    {
        qWarning() << "[addDir] failed" << path;
    }
}

void Bridge::onRemoveDirClicked()
{
    const QString path = textOf("dirInput");
    if (td_.removeDir(path.toStdString()))
    {
        qInfo() << "[removeDir] ok" << path;
        pushDirList();
    }
    else
    {
        qWarning() << "[removeDir] failed" << path;
    }
}

void Bridge::onAddTagClicked()
{
    const QString type = textOf("typeInput");
    const QString tag = textOf("tagInput");
    if (td_.addTagToList(type.toStdString(), tag.toStdString()))
    {
        qInfo() << "[addTag] ok" << type << tag;
        pushTagList();
    }
    else
    {
        qWarning() << "[addTag] failed" << type << tag;
    }
}

void Bridge::onAddTypeClicked()
{
    const QString type = textOf("typeInput");
    const QString color = textOf("colorInput");
    if (td_.addTypeToList(type.toStdString(), color.toStdString()))
    {
        qInfo() << "[addType] ok" << type << color;
        pushTagList();
    }
    else
    {
        qWarning() << "[addType] failed" << type;
    }
}

// clearButton: 清空 包含/排除/只有 三容器标签列表
void Bridge::onClearClicked()
{
    const char *names[] = { "includeContainer", "excludeContainer", "onlyContainer" };
    for (const char *name : names)
    {
        QObject *container = findObject(name);
        if (container)
        {
            QQmlProperty(container, "tagList").write(QVariantList());
            qInfo() << "[clear] 已清空" << name;
        }
    }
}

void Bridge::onRemoveTagClicked()
{
    const QString tag = textOf("tagInput");
    if (td_.removeTag(tag.toStdString()))
    {
        qInfo() << "[removeTag] ok" << tag;
        pushTagList();
    }
    else
    {
        qWarning() << "[removeTag] failed" << tag;
    }
}

void Bridge::onRemoveTypeClicked()
{
    const QString type = textOf("typeInput");
    if (td_.removeType(type.toStdString()))
    {
        qInfo() << "[removeType] ok" << type;
        pushTagList();
    }
    else
    {
        qWarning() << "[removeType] failed" << type;
    }
}

// resetTypeColor: typeInput 的类型 + colorInput 的颜色 -> TagServe::setTypeColor
void Bridge::onResetTypeColorClicked()
{
    const QString type = textOf("typeInput");
    const QString color = textOf("colorInput");
    if (td_.setTypeColor(type.toStdString(), color.toStdString()))
    {
        qInfo() << "[resetTypeColor] ok" << type << color;
        pushTagList();
        pushFileList();
    }
    else
    {
        qWarning() << "[resetTypeColor] failed" << type << color << ts_.getTagError();
    }
}

void Bridge::onDirDoubleClicked(const QString &path)
{
    if (td_.getDirFile(path.toStdString()))
    {
        qInfo() << "[getDirFile] ok" << path << "files:" << td_.path_tags_.size();
        pushFileList();
    }
    else
    {
        qWarning() << "[getDirFile] failed(不在授权目录内?)" << path;
    }
}

// FileContainer 双击某行: 目录 -> 打开该目录; 文件 -> 打开所在目录并高亮选中(Windows: explorer /select)
void Bridge::onFileDoubleClicked(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        qWarning() << "[open] 路径不存在:" << path;
        return;
    }

    if (info.isDir())
    {
        // 目录: 直接打开
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath())))
        {
            qInfo() << "[open] 目录 ok" << info.absoluteFilePath();
        }
        else
        {
            qWarning() << "[open] 打开目录失败:" << info.absoluteFilePath();
        }
        return;
    }

    // 文件: 打开所在目录并高亮选中
#ifdef Q_OS_WIN
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(info.absoluteFilePath());
    if (QProcess::startDetached(QStringLiteral("explorer"), args))
    {
        qInfo() << "[open] 定位文件 ok" << info.absoluteFilePath();
    }
    else
    {
        qWarning() << "[open] 定位文件失败:" << info.absoluteFilePath();
    }
#else
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath())))
    {
        qInfo() << "[open] 所在目录 ok" << info.absolutePath();
    }
    else
    {
        qWarning() << "[open] 打开所在目录失败:" << info.absolutePath();
    }
#endif
}

// refreshButton: 强制刷新
// 重新校验目录(DirectoryConfigManager 移除失效目录 逐目录 isPathAllowed 校验)
// 重新加载标签库(TagServe::reLoadTag)
// 重新加载数据库根目录(TagServe::reLoadRoot 同步文件系统)
// 前端刷新：DirContainer / LibraryTag（FileContainer 不刷新）
void Bridge::onRefreshClicked()
{
    qInfo() << "[refresh] 开始强制刷新...";

    dm_.clearInvalidPath();
    dm_.saveToFile();

    if (!ts_.reLoadTag("./config/tag.json"))
    {
        qWarning() << "[refresh] reLoadTag failed:" << ts_.getTagError();
    }

    if (!ts_.reLoadRoot(dm_.getValidDirList()))
    {
        qWarning() << "[refresh] reLoadRoot failed:" << ts_.getDBError();
    }

    td_.updataDirList();
    td_.updataTagList();
    pushDirList();
    pushTagList();

    qInfo() << "[refresh] 完成";
}

// config_ -> setWindow 控件: 只读项填真实值 可编辑项(等待时间/下载路径/语言)同步控件初值
void Bridge::pushConfig()
{
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
    // 语言列表与当前语言: 由 pushLanguageList 按语言文件目录填充(同 test.cpp loadLanguageList)
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
        qInfo() << "[saveConfig] ok: language=" << config_.default_language_.c_str()
                << "wait=" << config_.server_waiting_time_.count()
                << "download=" << config_.download_path_.string().c_str();
    }
    else
    {
        qWarning() << "[saveConfig] failed:" << config_.error_string_.c_str();
    }

    qInfo() << "[app] 保存配置完成, 退出程序(部分配置重启后生效)";
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
        qInfo() << "[convertmode] ok ->" << (to == TagFileManager::StoreMode::Filename ? "Filename" : "Sidecar");
        pushConfig(); // setWindow 的 TagMode 只读项同步新模式
    }
    else
    {
        qWarning() << "[convertmode] failed:" << ts_.getLastError().c_str() << "/" << ts_.getFileError().c_str();
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
    { "settings.version", "版本:" },
    { "settings.port", "广播端口:" },
    { "settings.magic", "魔术字:" },
    { "settings.tagMode", "标签模式:" },
    { "settings.wait", "等待时间(分钟):" },
    { "settings.download", "默认下载路径:" },
    { "settings.language", "语言:" },
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
    };
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

// LanguageManager::loadLanguageList 的结果 -> setWindow 语言下拉(同 test.cpp language list)
// 并选中 config_.default_language_ 对应的项(找不到时停在 0)
void Bridge::pushLanguageList()
{
    if (!root_)
    {
        return;
    }
    const auto &langs = language_.getLanguagesList(); // 目录下 *.json 的 stem 列表
    QVariantList names;
    for (const auto &n : langs)
    {
        names << QString::fromStdString(n);
    }
    QQmlProperty(root_, "languageNames").write(names);

    if (QObject *combo = findObject("languageCombo"))
    {
        int idx = 0;
        for (size_t i = 0; i < langs.size(); ++i)
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
        qWarning() << "[sync] syncWindow not found, serverQueue 未推送";
        return;
    }
    QVariantList list;
    const auto queue = s_server_.getTaskQueue(); // 线程安全 返回拷贝
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
    const bool ok = s_server_.start(name, 0, ec, [this](bool success, std::error_code e)
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
        qInfo() << "[server] start ok name =" << QString::fromStdString(name);
        setStatusLabel("serverStatusLabel", uiText("status.server.start", "服务器已启动 (%1)").arg(QString::fromStdString(name)));
    }
    else
    {
        qWarning() << "[server] start failed:" << ec.message().c_str();
        setStatusLabel("serverStatusLabel", uiText("status.server.startFail", "启动失败: %1").arg(QString::fromStdString(ec.message())));
    }
}

void Bridge::onServerStopClicked()
{
    std::error_code ec;
    s_server_.stop(ec);
    if (!ec)
    {
        qInfo() << "[server] stop ok";
        setStatusLabel("serverStatusLabel", uiText("status.server.stop", "服务器已停止"));
    }
    else
    {
        qWarning() << "[server] stop failed:" << ec.message().c_str();
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

    // 无效值检测: 必须存在且为目录 通过后归一化为绝对路径再入队
    std::error_code ec;
    std::filesystem::path p(dir.toStdString());
    const auto abs = std::filesystem::absolute(p, ec);
    if (ec || !std::filesystem::is_directory(abs, ec) || ec)
    {
        qWarning() << "[server] add failed(无效目录):" << dir;
        setStatusLabel("serverStatusLabel", uiText("status.server.invalidDir", "无效目录: %1").arg(dir));
        return;
    }

    const auto norm = abs.lexically_normal();
    s_server_.enqueueDirectory(norm);
    qInfo() << "[server] add" << QString::fromStdString(norm.generic_u8string());
    setStatusLabel("serverStatusLabel", uiText("status.server.enqueued", "已入队: %1").arg(QString::fromStdString(norm.generic_u8string())));
    pushServerQueue();
}

void Bridge::onServerDisconnectClicked()
{
    std::error_code ec;
    s_server_.disconnect(ec);
    if (!ec)
    {
        qInfo() << "[server] disconnect ok";
        setStatusLabel("serverStatusLabel", uiText("status.server.disconnected", "已断开连接设备"));
    }
    else
    {
        qWarning() << "[server] disconnect failed:" << ec.message().c_str();
    }
}

void Bridge::onServerTipArrived(const QString &msg)
{
    qInfo() << msg;
    setStatusLabel("serverStatusLabel", msg);
}

void Bridge::pushServerList()
{
    QObject *w = findObject("syncWindow");
    if (!w)
    {
        qWarning() << "[sync] syncWindow not found, serverList 未推送";
        return;
    }
    QVariantList list;
    for (const auto &s : s_client_.getServers()) // 线程安全
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
    const auto servers = s_client_.scanServers();
    pushServerList();
    qInfo() << "[client] scan done servers:" << servers.size();
    setStatusLabel("clientStatusLabel", uiText("status.client.scanDone", "扫描完成 %1 台设备").arg(static_cast<int>(servers.size())));
}

void Bridge::onClientDownloadClicked()
{
    QObject *lv = findObject("serverListView");
    const int index = lv ? QQmlProperty(lv, "currentIndex").read().toInt() : -1;
    const auto servers = s_client_.getServers();
    if (index < 0 || static_cast<size_t>(index) >= servers.size())
    {
        qWarning() << "[client] download failed: 未选中有效设备 index =" << index;
        setStatusLabel("clientStatusLabel", uiText("status.client.noSelect", "请先扫描并选中一台设备"));
        return;
    }
    const size_t idx = static_cast<size_t>(index);
    qInfo() << "[client] download start index =" << index << "server =" << servers[idx].name_.c_str();
    setStatusLabel("clientStatusLabel", uiText("status.client.downloading", "正在下载 %1 ...").arg(QString::fromStdString(servers[idx].name_)));
    s_client_.startDownload(idx, [this](bool success, std::error_code e)
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
            msg = uiText("status.client.failed", "客户端: 下载失败/断开");
        }
        emit syncClientTip(msg);
    });
}

void Bridge::onClientClearClicked()
{
    s_client_.clearDownloadRecords();
    qInfo() << "[client] clear records";
    setStatusLabel("clientStatusLabel", uiText("status.client.cleared", "已清除下载记录"));
}

void Bridge::onClientDisconnectClicked()
{
    s_client_.disconnect();
    qInfo() << "[client] disconnect";
    setStatusLabel("clientStatusLabel", uiText("status.client.disconnected", "已断开连接"));
}

void Bridge::onClientTipArrived(const QString &msg)
{
    qInfo() << msg;
    setStatusLabel("clientStatusLabel", msg);
}

void Bridge::bindTo(QObject *root)
{
    root_ = root;
    fileContainer_ = findObject("fileContainer");
    dirContainer_ = findObject("dirContainer");
    libraryTag_ = findObject("libraryTag");
    includeContainer_ = findObject("includeContainer");
    excludeContainer_ = findObject("excludeContainer");
    onlyContainer_ = findObject("onlyContainer");

    qInfo() << "[bridge] controls found:"
            << (fileContainer_ ? "fileContainer" : "-")
            << (dirContainer_ ? "dirContainer" : "-")
            << (libraryTag_ ? "libraryTag" : "-")
            << (includeContainer_ ? "include" : "-")
            << (excludeContainer_ ? "exclude" : "-")
            << (onlyContainer_ ? "only" : "-")
            << (findObject("searchButton") ? "searchButton" : "-")
            << (findObject("addDirBtn") ? "addDirBtn" : "-")
            << (findObject("addTagBtn") ? "addTagBtn" : "-")
            << (findObject("addTypeBtn") ? "addTypeBtn" : "-")
            << (findObject("removeTagBtn") ? "removeTagBtn" : "-")
            << (findObject("removeTypeBtn") ? "removeTypeBtn" : "-")
            << (findObject("removeDirBtn") ? "removeDirBtn" : "-")
            << (findObject("resetTypeColor") ? "resetTypeColor" : "-")
            << (findObject("pathInput") ? "pathInput" : "-");

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
        QObject::connect(fileContainer_, SIGNAL(fileDoubleClicked(QString)), this, SLOT(onFileDoubleClicked(QString)));
    }

    // DirContainer: 双击目录 -> getDirFile -> 刷新 FileContainer
    if (dirContainer_)
    {
        QObject::connect(dirContainer_, SIGNAL(dirDoubleClicked(QString)), this, SLOT(onDirDoubleClicked(QString)));
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

    // 初始填充（后端磁盘数据 -> 控件 + 语言字典/语言列表 -> window）
    td_.updataDirList();
    td_.updataTagList();
    pushConfig();
    pushLanguageList();
    pushUiText();
    pushDirList();
    pushTagList();
}
