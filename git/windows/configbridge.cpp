#include "configbridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <system_error>

namespace
{
// LanguageManager 用这个哨兵表示"字典里没有这个 id"（见 core/src/language_manager.cpp）
const char *MISSING_STRING_SENTINEL = "MISSING_STRING";
}

ConfigBridge::ConfigBridge(QObject *parent)
    : QObject(parent)
      // 构造里就读 ./config/config.json：文件不存在时 ConfigLoader 自己落一份默认值
    , config_()
      // 受管目录白名单 ./config/path.json（读取时会顺带算好每个目录的有效性）
    , dm_("./config/path.json")
      // 构造里就 loadLanguageList("./language")：扫目录拿到可选语言列表
    , language_("./language")
      // 标签库 + 索引库；根列表与存储模式都来自上面两个（与旧工程 Bridge 的构造完全一致）
    , ts_(dm_.getValidDirList(), config_.tag_mode_, "./config/tag.json", "./config/index.db")
    , last_error_()
      // 日志直接在本类里建：与桥同生共死，不需要谁登记谁注销
    , log_()
    , last_logged_()
{
    // 按配置里的 DefaultLanguage 加载一次字典；失败就当"没有 core 字典"，
    // QML 侧的 Lang.qml 会自动落回自带的兜底表，界面不会因此空白
    if (!config_.default_language_.empty())
    {
        if (!language_.loadLanguage(config_.default_language_))
        {
            last_error_ = QString::fromUtf8(language_.getLastError());
        }
    }
    else
    {
        last_error_ = QStringLiteral("[warning] DefaultLanguage is empty in config.json");
    }

    if (last_error_.isEmpty() && !config_.error_string_.empty())
    {
        last_error_ = QString::fromUtf8(config_.error_string_);
    }

    if (last_error_.isEmpty() && !language_.getLastError().empty())
    {
        last_error_ = QString::fromUtf8(language_.getLastError());
    }

    // 构造函数里发信号没人接得住，排到事件循环第一次转起来之后再发，
    // 那时 QML 早已建好 Store 的 Connections，启动期的警告也能弹成 Toast
    if (!last_error_.isEmpty())
    {
        const QString startup_message = last_error_;
        QMetaObject::invokeMethod(
            this,
            [this, startup_message]() {
                emit errorOccurred(startup_message);
            },
            Qt::QueuedConnection);
    }
    else
    {
        setTip(QStringLiteral("[tip] config loaded: version %1, language %2, FontSize %3, Theme %4")
                   .arg(version())
                   .arg(effectiveLanguageName())
                   .arg(config_.font_size_)
                   .arg(config_.theme_));
    }

    // 输入流截获：与旧工程 Bridge::bindTo 里那句 qApp->installEventFilter(this) 等价。
    // 本类活得和应用一样久（QML 引擎持有的单例），析构里摘掉，不会留下悬空过滤器
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // 工作线程的回调只发信号（syncStateChanged）—— 那些信号是跨线程发的，auto 连接会自动排队，
    // 所以这里接的回调一定跑在主线程上：趁"有回报"的时候把队列/设备快照重新搬一遍。
    // SyncServer::disconnect() 只是置位断开请求、由工作线程在下一个有界阻塞点真正清空队列，
    // 所以断开后队列快照必须等这条回报才更新，不能在 disconnect() 之后立刻读。
    connect(this, &ConfigBridge::syncStateChanged, this,
            [this]()
            {
                refreshServerQueue();

                // 状态一变就立刻落一条：下载的结局（成功 / 失败原因）是工作线程通过这个信号
                // 排队回来的，不在这里记的话日志里只留得住"开始下载"，看不到结果
                if (s_client_)
                {
                    logNow(QStringLiteral("[tip] sync state: downloading=%1 connected=%2 clientErr=[%3]")
                               .arg(clientDownloading() ? 1 : 0)
                               .arg(connectedServerIndex())
                               .arg(QString::fromStdString(s_client_->getLastError()))
                               .toStdString());
                }
            });

    // 任务(入队目录)完成回报：工作线程发的信号排队到这里，在主线程记下"具体下载目录"。
    // 之前只声明了 last_task_dir_ 却从来没人写它，所以"加入管理目录"永远退回下载根目录。
    connect(this, &ConfigBridge::clientTaskReported, this,
            [this](const QString &task_dir)
            {
                if (task_dir.isEmpty())
                {
                    return;
                }

                last_task_dir_ = task_dir;
                logNow(QStringLiteral("[tip] sync last task dir -> %1").arg(task_dir).toStdString());
                emit syncStateChanged();
            });
}

ConfigBridge::~ConfigBridge()
{
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->removeEventFilter(this);
    }

    // 先停工作线程再销毁：SyncServer / SyncClient 的回调捕获了 this，
    // 顺序反了就会让回调落到已经析构一半的对象上
    if (s_server_)
    {
        std::error_code ec;
        s_server_->stop(ec);
    }

    if (s_client_)
    {
        s_client_->disconnect();
    }

    // 真正落盘由 ConfigLoader 的析构负责，这里不用再存一次
}

// 输入流截获：鼠标抬起 / 按键抬起 -> 排到下一轮事件循环再收，**不消费事件**（return false）。
// 之所以要排到下一轮：等本次交互的处理逻辑跑完，才能读到这次交互真正产生的那条 error
bool ConfigBridge::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (event != nullptr && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease))
    {
        QTimer::singleShot(0, this, [this]() { collectLogMessages(); });
    }

    return false;
}

// 逐个读各类的 error_string_：与旧工程 bridge.cpp 的 collectLogMessages 同一套写法，
// 能读到的就是本类持有的这几个 core 对象（Bridge 自己 + ConfigLoader + DirectoryConfigManager +
// LanguageManager + TagServe + FileDatabase + TagLibrary + TagFileManager）
void ConfigBridge::collectLogMessages()
{
    logOne("ConfigBridge", last_error_.toStdString());
    logOne("ConfigLoader", config_.error_string_);
    logOne("DirectoryConfigManager", dm_.getLastError());
    logOne("LanguageManager", language_.getLastError());
    logOne("TagServe", ts_.getLastError());
    logOne("FileDatabase", ts_.getDBError());
    logOne("TagLibrary", ts_.getTagError());
    logOne("TagFileManager", ts_.getFileError());

    // 同步对象是惰性创建的，没建过就不读（与旧工程里 s_server_ / s_client_ 的非空判断一致）
    if (s_server_)
    {
        logOne("SyncServer", s_server_->getLastError());
    }

    if (s_client_)
    {
        logOne("SyncClient", s_client_->getLastError());
    }
}

void ConfigBridge::logOne(const std::string &class_name, const std::string &message)
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
}

// 立刻写盘（不参与"与上次相同就跳过"的去重）：给同步这种异步流程的关键节点用。
// 只靠 eventFilter 的"交互后收集"时，用户点完最后一个按钮就关窗口的话，
// 那条记录会一直留在内存里 —— 排查时日志里干干净净，什么都看不到。
void ConfigBridge::logNow(const std::string &message)
{
    if (message.empty())
    {
        return;
    }

    log_.write("ConfigBridge", message);
    // 同步一下去重基线，免得下一次 collectLogMessages 把同一条再写一遍
    last_logged_["ConfigBridge"] = last_error_.toStdString();
}

QString ConfigBridge::version() const
{
    return QString::fromUtf8(config_.version_);
}

QString ConfigBridge::tagMode() const
{
    const bool filename_mode = (config_.tag_mode_ == TagFileManager::StoreMode::Filename);
    return filename_mode ? QStringLiteral("Filename") : QStringLiteral("Sidecar");
}

int ConfigBridge::serverWaitingTime() const
{
    return static_cast<int>(config_.server_waiting_time_.count());
}

QString ConfigBridge::downloadPath() const
{
    return QString::fromStdString(config_.download_path_.string());
}

int ConfigBridge::broadcastPort() const
{
    return static_cast<int>(config_.broadcast_port_);
}

QString ConfigBridge::broadcastMagicWord() const
{
    return QString::fromUtf8(config_.broadcast_magic_word_);
}

QStringList ConfigBridge::languageList() const
{
    QStringList list;
    for (const std::string &name : language_.getLanguagesList())
    {
        list << QString::fromUtf8(name);
    }

    // 目录遍历顺序没有保证，排一下让设置页"循环切下一种语言"有确定次序
    list.sort(Qt::CaseInsensitive);
    return list;
}

QString ConfigBridge::language() const
{
    return effectiveLanguageName();
}

int ConfigBridge::fontSize() const
{
    return config_.font_size_;
}

void ConfigBridge::setFontSize(int size)
{
    if (size < MIN_FONT_SIZE || size > MAX_FONT_SIZE)
    {
        reportError(QStringLiteral("[warning] FontSize out of range %1-%2: %3")
                        .arg(MIN_FONT_SIZE)
                        .arg(MAX_FONT_SIZE)
                        .arg(size));
        // 值没变，但 QML 那边的绑定需要重算一次才能把越界的输入弹回真值
        emit fontSizeChanged();
        return;
    }

    if (size == config_.font_size_)
    {
        return;
    }

    config_.font_size_ = size;
    if (persist())
    {
        setTip(QStringLiteral("[tip] FontSize -> %1 (saved to config.json)").arg(size));
    }
    emit fontSizeChanged();
}

int ConfigBridge::theme() const
{
    return config_.theme_;
}

void ConfigBridge::setTheme(int theme)
{
    if (theme < 0)
    {
        reportError(QStringLiteral("[warning] Theme cannot be negative: %1").arg(theme));
        emit themeChanged();
        return;
    }

    if (theme == config_.theme_)
    {
        return;
    }

    config_.theme_ = theme;
    if (persist())
    {
        setTip(QStringLiteral("[tip] Theme -> %1 (saved to config.json)").arg(theme));
    }
    emit themeChanged();
}

bool ConfigBridge::setLanguage(const QString &language_name)
{
    const QString trimmed = language_name.trimmed();
    if (trimmed.isEmpty())
    {
        reportError(QStringLiteral("[warning] empty language name"));
        return false;
    }

    const std::string name_utf8 = trimmed.toUtf8().toStdString();
    const std::vector<std::string> &available = language_.getLanguagesList();
    if (std::find(available.begin(), available.end(), name_utf8) == available.end())
    {
        reportError(QStringLiteral("[warning] unknown language: %1").arg(trimmed));
        return false;
    }

    if (!language_.loadLanguage(name_utf8))
    {
        reportError(QString::fromUtf8(language_.getLastError()));
        return false;
    }

    // 语言文件里的 name 字段可能与文件名主干不同，配置里存主干：下次启动 loadLanguage 才找得到文件
    config_.default_language_ = name_utf8;
    if (persist())
    {
        setTip(QStringLiteral("[tip] language -> %1 (saved to config.json)").arg(effectiveLanguageName()));
    }

    emit languageChanged();
    return true;
}

bool ConfigBridge::reloadLanguageList()
{
    if (!language_.loadLanguageList("./language"))
    {
        reportError(QString::fromUtf8(language_.getLastError()));
        return false;
    }

    setTip(QStringLiteral("[tip] language list reloaded: %1 entries").arg(languageList().size()));
    emit languageListChanged();
    return true;
}

QString ConfigBridge::text(const QString &id) const
{
    if (id.isEmpty())
    {
        return QString();
    }

    const std::string &value = language_.getString(id.toUtf8().toStdString());
    // 哨兵 -> 空串：调用方（Lang.qml）看到空串就落回自己的兜底表
    if (value.empty() || value == MISSING_STRING_SENTINEL)
    {
        return QString();
    }

    return QString::fromUtf8(value);
}

bool ConfigBridge::save()
{
    if (!persist())
    {
        return false;
    }

    setTip(QStringLiteral("[tip] config saved to ./config/config.json"));
    return true;
}

QString ConfigBridge::lastError() const
{
    return last_error_;
}

// ============================================================================
// 标签库 / 索引（core 的 DirectoryConfigManager + TagServe）
// ============================================================================

int ConfigBridge::fileCount() const
{
    return ts_.getFileCount();
}

int ConfigBridge::dirCount() const
{
    return static_cast<int>(dm_.getDirectories().size());
}

QString ConfigBridge::lastReport() const
{
    return last_report_;
}

int ConfigBridge::reportValue(const QString &key) const
{
    // 直接查 core 的 out 参数存下来的那份数据，不解析任何字符串
    bool ok = false;
    const int number = last_values_.value(key, -1).toInt(&ok);
    return ok ? number : -1;
}

void ConfigBridge::setReport(const QVariantMap &values)
{
    last_values_ = values;

    // QVariantMap 按 key 排序遍历，拼出来的文本是稳定的
    QStringList parts;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        parts << QStringLiteral("%1=%2").arg(it.key()).arg(it.value().toInt());
    }

    last_report_ = parts.join(QLatin1Char(' '));
}

bool ConfigBridge::convertMode()
{
    const TagFileManager::StoreMode from_mode = config_.tag_mode_;
    const TagFileManager::StoreMode to_mode = (from_mode == TagFileManager::StoreMode::Sidecar)
                                                  ? TagFileManager::StoreMode::Filename
                                                  : TagFileManager::StoreMode::Sidecar;

    int converted = -1;
    int failed = -1;

    // keep_old = false：先整根写一遍新格式，全成功再回头删旧格式（中途失败会中止，不会半新半旧）
    if (!ts_.convertMode(from_mode, to_mode, false, &converted, &failed))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        emit libraryChanged(); // 失败也可能已经改动过一部分文件，让界面重新取一次数
        return false;
    }

    // core 成功后已经把默认模式切成 to_mode 并重建了索引，这里把新模式同步进 config.json
    config_.tag_mode_ = to_mode;
    if (!persist())
    {
        return false;
    }

    // core 只回 bool，条目数一律走带 nullptr 默认值的 out 参数带出来（不解析它的 error_string_）
    QVariantMap report;
    report[QStringLiteral("converted")] = converted;
    report[QStringLiteral("failed")] = failed;
    setReport(report);

    setTip(QStringLiteral("[tip] mode -> %1, %2").arg(tagMode()).arg(last_report_));
    emit libraryChanged();
    return true;
}

bool ConfigBridge::refreshIndex()
{
    int indexed = -1;
    int roots = -1;

    // reLoadRoot(全部有效目录)：内部先 clearAll 再按磁盘重扫，等价"重建索引"
    if (!ts_.reLoadRoot(dm_.getValidDirList(), &indexed, &roots))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        emit libraryChanged();
        return false;
    }

    QVariantMap report;
    report[QStringLiteral("indexed")] = indexed;
    report[QStringLiteral("roots")] = roots;
    setReport(report);

    setTip(QStringLiteral("[tip] index refreshed, %1").arg(last_report_));
    emit libraryChanged();
    return true;
}

bool ConfigBridge::clearInvalidData()
{
    // 两步都只动数据库记录：cleanupInvalid 删掉磁盘上已不存在的记录，clearRepeat 去重
    int removed = -1;
    if (!ts_.cleanupInvalid(&removed))
    {
        reportError(QString::fromUtf8(ts_.getDBError()));
        emit libraryChanged();
        return false;
    }

    int deduped = -1;
    if (!ts_.clearRepeat(&deduped))
    {
        reportError(QString::fromUtf8(ts_.getDBError()));
        emit libraryChanged();
        return false;
    }

    QVariantMap report;
    report[QStringLiteral("removed")] = removed;
    report[QStringLiteral("deduped")] = deduped;
    setReport(report);

    setTip(QStringLiteral("[tip] database cleaned, %1").arg(last_report_));
    emit libraryChanged();
    return true;
}

bool ConfigBridge::exportLibrary(const QString &target_url)
{
    const std::filesystem::path target_path = localPathFromUrl(target_url);
    if (target_path.empty())
    {
        reportError(QStringLiteral("[warning] export path is empty"));
        return false;
    }

    // 先把内存里的标签库写回 tag.json，免得导出的是上一次落盘的旧内容
    if (!ts_.saveTag())
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        return false;
    }

    const std::filesystem::path source_path = ts_.getTagPath();
    std::error_code ec;
    std::filesystem::copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        reportError(QStringLiteral("[error] export failed: %1").arg(QString::fromStdString(ec.message())));
        return false;
    }

    // 成功回报：导出了多少字节（core 不报，这里按复制后的实际大小报）
    std::error_code size_ec;
    const auto exported_bytes = std::filesystem::file_size(target_path, size_ec);

    QVariantMap report;
    report[QStringLiteral("bytes")] = size_ec ? 0 : static_cast<qulonglong>(exported_bytes);
    setReport(report);

    setTip(QStringLiteral("[tip] tag library exported to %1, %2")
               .arg(QString::fromStdString(target_path.u8string()))
               .arg(last_report_));
    return true;
}

bool ConfigBridge::importLibrary(const QString &source_url)
{
    const std::filesystem::path source_path = localPathFromUrl(source_url);
    if (source_path.empty())
    {
        reportError(QStringLiteral("[warning] import path is empty"));
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(source_path, ec) || ec)
    {
        reportError(QStringLiteral("[error] import file not found: %1").arg(QString::fromStdString(source_path.u8string())));
        return false;
    }

    int types = -1;
    int tags = -1;

    // mergeTags 内部合并成功后立刻写盘（类型 / 标签全局唯一，只补本库没有的）
    if (!ts_.mergeTags(source_path, &types, &tags))
    {
        // 失败说明由 TagServe 自己收着（它把 TagLibrary 的错误转写进了自己的 error_string_）
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    QVariantMap report;
    report[QStringLiteral("types")] = types;
    report[QStringLiteral("tags")] = tags;
    setReport(report);

    setTip(QStringLiteral("[tip] tag library imported from %1, %2")
               .arg(QString::fromStdString(source_path.u8string()))
               .arg(last_report_));
    emit libraryChanged();
    return true;
}

std::filesystem::path ConfigBridge::localPathFromUrl(const QString &url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile())
    {
        return std::filesystem::u8path(parsed.toLocalFile().toUtf8().toStdString());
    }

    // 也接受直接给的本地路径（QML 侧一律按 UTF-8 传进来）
    return std::filesystem::u8path(url.toUtf8().toStdString());
}

QString ConfigBridge::effectiveLanguageName() const
{
    const std::string &loaded = language_.getLanguageName();
    if (!loaded.empty())
    {
        return QString::fromUtf8(loaded);
    }

    return QString::fromUtf8(config_.default_language_);
}

bool ConfigBridge::persist()
{
    if (!config_.saveConfig())
    {
        reportError(QString::fromUtf8(config_.error_string_));
        return false;
    }

    emit configChanged();
    return true;
}

void ConfigBridge::reportError(const QString &message)
{
    if (message.isEmpty())
    {
        return;
    }

    last_error_ = message;
    emit errorOccurred(last_error_);
}

void ConfigBridge::setTip(const QString &message)
{
    // 提示不算"失败"：只更新 error_string_（由本类的 collectLogMessages 在下次鼠标/键盘交互后收进日志），
    // 不发 errorOccurred，所以不会弹 Toast
    last_error_ = message;
}

// ============================================================================
// 同步：服务端
// ============================================================================

bool ConfigBridge::serverRunning() const
{
    return server_running_flag_;
}

QVariantList ConfigBridge::serverQueue() const
{
    return server_queue_;
}

void ConfigBridge::ensureServer()
{
    if (s_server_)
    {
        return;
    }

    // 广播端口 / 魔术字 / 空队列等待时长都来自 config.json（与旧工程一致）
    s_server_ = std::make_unique<SyncServer>(config_.broadcast_port_,
                                            config_.broadcast_magic_word_,
                                            config_.server_waiting_time_);

    s_server_->setTaskCallback([this](const TaskReport &report)
                               {
                                   // 工作线程：只发信号（Qt 会自动排队到主线程），不碰任何成员
                                   emit syncMessage(QStringLiteral("sync.task_done"),
                                                    QString::fromStdString(report.name_));
                               });
}

void ConfigBridge::refreshServerQueue()
{
    QVariantList list;

    if (s_server_)
    {
        for (const std::filesystem::path &dir : s_server_->getTaskQueue())
        {
            QVariantMap item;
            item.insert(QStringLiteral("name"), QString::fromStdString(dir.filename().u8string()));
            item.insert(QStringLiteral("path"), QString::fromStdString(dir.generic_u8string()));
            list.append(item);
        }
    }

    server_queue_ = list;
    emit serverQueueChanged();
}

bool ConfigBridge::toggleServer(const QString &server_name)
{
    logNow(QStringLiteral("[tip] sync toggleServer entered: name=[%1] running=%2")
               .arg(server_name)
               .arg(server_running_flag_ ? 1 : 0)
               .toStdString());

    if (server_running_flag_)
    {
        if (!s_server_)
        {
            server_running_flag_ = false;
            emit syncStateChanged();
            return true;
        }

        std::error_code ec;
        s_server_->stop(ec);
        server_running_flag_ = false;

        if (ec)
        {
            reportError(QString::fromUtf8(s_server_->getLastError()));
            emit syncStateChanged();
            refreshServerQueue();
            return false;
        }

        setTip(QStringLiteral("[tip] sync server stopped"));
        emit syncMessage(QStringLiteral("status.server.stop"), QString());
        emit syncStateChanged();
        refreshServerQueue();
        return true;
    }

    ensureServer();

    QString name = server_name.trimmed();
    if (name.isEmpty())
    {
        // 与旧工程一致：名字留空就兜底成默认名
        name = QStringLiteral("tagmeow");
    }

    std::error_code ec;
    const bool ok = s_server_->start(name.toStdString(), 0, ec, [this](bool success, std::error_code e)
                                     {
                                         // 工作线程回调：只发信号
                                         if (success)
                                         {
                                             const bool queue_empty =
                                                 (e == std::make_error_code(std::errc::no_message_available));
                                             emit syncMessage(queue_empty
                                                                  ? QStringLiteral("sync.server_waiting")
                                                                  : QStringLiteral("sync.server_session_end"),
                                                              QString());
                                         }
                                         else
                                         {
                                             emit syncMessage(QStringLiteral("sync.server_session_error"), QString());
                                         }
                                         emit syncStateChanged();
                                     });

    if (!ok)
    {
        reportError(QStringLiteral("[error] sync server start failed: %1")
                        .arg(QString::fromStdString(ec.message())));
        emit syncMessage(QStringLiteral("sync.start_failed"),
                         QString::fromStdString(ec.message()));
        emit syncStateChanged();
        return false;
    }

    server_running_flag_ = true;
    setTip(QStringLiteral("[tip] sync server started: %1").arg(name));
    emit syncMessage(QStringLiteral("status.server.started"), QString());
    emit syncStateChanged();
    refreshServerQueue();
    return true;
}

bool ConfigBridge::enqueueServerDir(const QString &dir_path)
{
    logNow(QStringLiteral("[tip] sync enqueueServerDir entered: [%1] running=%2")
               .arg(dir_path)
               .arg(server_running_flag_ ? 1 : 0)
               .toStdString());

    if (!server_running_flag_ || !s_server_)
    {
        reportError(QStringLiteral("[warning] enqueue rejected: sync server is not running"));
        emit syncMessage(QStringLiteral("sync.server_off_hint"), QString());
        return false;
    }

    const std::filesystem::path raw = localPathFromUrl(dir_path);
    if (raw.empty())
    {
        emit syncMessage(QStringLiteral("sync.dir_invalid"), dir_path);
        return false;
    }

    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(raw, ec);
    if (ec || !std::filesystem::is_directory(abs, ec) || ec)
    {
        reportError(QStringLiteral("[warning] enqueue rejected, not a directory: %1")
                        .arg(QString::fromStdString(raw.u8string())));
        emit syncMessage(QStringLiteral("sync.dir_invalid"), dir_path);
        return false;
    }

    const std::filesystem::path norm = abs.lexically_normal();
    s_server_->enqueueDirectory(norm);
    setTip(QStringLiteral("[tip] sync server enqueued: %1")
               .arg(QString::fromStdString(norm.generic_u8string())));

    // 入队后立刻把队列快照推给界面（"有回报就刷新渲染"）
    refreshServerQueue();
    emit syncMessage(QStringLiteral("sync.enqueued"), QString::fromStdString(norm.generic_u8string()));
    return true;
}

bool ConfigBridge::disconnectServerClient()
{
    if (!s_server_)
    {
        reportError(QStringLiteral("[warning] disconnect rejected: sync server was never started"));
        return false;
    }

    std::error_code ec;
    s_server_->disconnect(ec);
    if (ec)
    {
        reportError(QStringLiteral("[error] sync server disconnect failed: %1")
                        .arg(QString::fromStdString(ec.message())));
        return false;
    }

    setTip(QStringLiteral("[tip] sync server disconnected the client"));
    emit syncMessage(QStringLiteral("status.server.disconnected"), QString());
    refreshServerQueue();
    emit syncStateChanged();
    return true;
}

// ============================================================================
// 同步：客户端
// ============================================================================

bool ConfigBridge::clientDownloading() const
{
    return s_client_ && s_client_->isDownloading();
}

int ConfigBridge::connectedServerIndex() const
{
    // 只在"正在连接/下载"期间才报下标：会话一结束就回到 -1，
    // 这样界面上的灰化会跟着会话自动恢复（is_busy_ 由 SyncClient 原子维护，跨线程读安全）
    return clientDownloading() ? connected_server_index_ : -1;
}

QVariantList ConfigBridge::clientServers() const
{
    return client_servers_;
}

QString ConfigBridge::lastTaskDir() const
{
    return last_task_dir_;
}

void ConfigBridge::ensureClient()
{
    if (s_client_)
    {
        return;
    }

    // 保存目录固定取 config.json 的 DownloadPath（默认 ./download）；
    // 想改只能整个重建（core 的 SyncClient 构造时固定下载路径）
    s_client_ = std::make_unique<SyncClient>(config_.broadcast_port_,
                                             config_.broadcast_magic_word_,
                                             std::filesystem::u8path(config_.download_path_.string()));

    s_client_->setTaskCallback([this](const TaskReport &report)
                               {
                                   // 工作线程：只发信号（排队回主线程后再记 last_task_dir_）
                                   const QString task_dir = QString::fromStdString(report.name_);
                                   emit clientTaskReported(task_dir);
                                   emit syncMessage(QStringLiteral("sync.task_done"), task_dir);
                               });
}

void ConfigBridge::refreshClientServers()
{
    QVariantList list;

    if (s_client_)
    {
        for (const ServerInfo &server : s_client_->getServers())
        {
            QVariantMap item;
            item.insert(QStringLiteral("name"), QString::fromStdString(server.name_));
            item.insert(QStringLiteral("ip"), QString::fromStdString(server.ip_));
            item.insert(QStringLiteral("port"), static_cast<int>(server.port_));
            list.append(item);
        }
    }

    client_servers_ = list;
    emit clientServersChanged();
}

bool ConfigBridge::scanServers()
{
    ensureClient();

    logNow("[tip] sync scanServers entered");
    emit syncMessage(QStringLiteral("sync.scanning"), QString());

    // core 的扫描是阻塞的（内部有总超时，最多两秒左右），与旧工程一样在主线程直接调
    const std::vector<ServerInfo> servers = s_client_->scanServers();
    refreshClientServers();

    for (const ServerInfo &s : servers)
    {
        logNow(QStringLiteral("[tip] sync scan hit: name=%1 ip=%2 port=%3")
                   .arg(QString::fromStdString(s.name_))
                   .arg(QString::fromStdString(s.ip_))
                   .arg(s.port_)
                   .toStdString());
    }

    setTip(QStringLiteral("[tip] sync scan done, servers: %1").arg(servers.size()));
    logNow(last_error_.toStdString());
    emit syncMessage(QStringLiteral("sync.scan_done"), QString::number(servers.size()));
    emit syncStateChanged();
    return true;
}

bool ConfigBridge::startDownload(int server_index)
{
    ensureClient();

    const std::vector<ServerInfo> &servers = s_client_->getServers();

    // 入口就打点：这一条能直接回答"界面上那一下点击到底有没有进到桥里"
    logNow(QStringLiteral("[tip] sync startDownload idx=%1 servers=%2 downloading=%3")
               .arg(server_index)
               .arg(servers.size())
               .arg(clientDownloading() ? 1 : 0)
               .toStdString());

    if (server_index < 0 || static_cast<std::size_t>(server_index) >= servers.size())
    {
        reportError(QStringLiteral("[warning] download rejected: no valid device selected, index %1")
                        .arg(server_index));
        emit syncMessage(QStringLiteral("sync.no_selection"), QString());
        return false;
    }

    const QString device_name = QString::fromStdString(servers[static_cast<std::size_t>(server_index)].name_);
    emit syncMessage(QStringLiteral("sync.downloading"), device_name);

    // 记下"当前连的是哪一台"：界面据此把这一行的下载按钮灰化
    connected_server_index_ = server_index;

    s_client_->startDownload(static_cast<std::size_t>(server_index),
                             [this](bool success, std::error_code e)
                             {
                                 // 工作线程回调：只发信号
                                 if (success)
                                 {
                                     const bool queue_empty =
                                         (e == std::make_error_code(std::errc::no_message_available));
                                     emit syncMessage(queue_empty
                                                          ? QStringLiteral("sync.download_empty")
                                                          : QStringLiteral("sync.download_done"),
                                                      QString());
                                 }
                                 else
                                 {
                                     emit syncMessage(QStringLiteral("sync.download_failed"),
                                                      QString::fromStdString(s_client_ ? s_client_->getLastError() : std::string()));
                                 }
                                 emit syncStateChanged();
                             });

    // 顺序很重要：SyncClient::startDownload 会在返回前**同步**把 is_busy_ 置位，
    // 所以这个信号必须发在它之后 —— 发在前面的话 QML 读 clientDownloading 还是 false，
    // 下载按钮就不会变灰，而下一个状态信号要等会话结束才来（整段下载期间都不置灰）。
    emit syncStateChanged();

    setTip(QStringLiteral("[tip] sync download started from %1").arg(device_name));
    return true;
}

void ConfigBridge::clearDownloadRecords()
{
    if (!s_client_)
    {
        emit syncMessage(QStringLiteral("sync.records_cleared"), QString());
        return;
    }

    s_client_->clearDownloadRecords();
    setTip(QStringLiteral("[tip] sync download records cleared"));
    emit syncMessage(QStringLiteral("sync.records_cleared"), QString());
    emit syncStateChanged();
}

void ConfigBridge::disconnectClient()
{
    if (s_client_)
    {
        s_client_->disconnect();
    }

    setTip(QStringLiteral("[tip] sync client disconnected"));
    emit syncMessage(QStringLiteral("sync.client_disconnected"), QString());
    emit syncStateChanged();
}

bool ConfigBridge::setDownloadPath(const QString &url)
{
    // 只有没在下载的时候才允许改保存目录
    if (clientDownloading())
    {
        reportError(QStringLiteral("[warning] download path change rejected while downloading"));
        emit syncMessage(QStringLiteral("sync.change_path_busy"), QString());
        return false;
    }

    const std::filesystem::path target = localPathFromUrl(url);
    if (target.empty())
    {
        emit syncMessage(QStringLiteral("sync.dir_invalid"), url);
        return false;
    }

    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(target, ec);
    if (ec)
    {
        reportError(QStringLiteral("[error] download path normalize failed: %1")
                        .arg(QString::fromStdString(ec.message())));
        return false;
    }

    std::filesystem::create_directories(abs, ec);
    if (ec)
    {
        reportError(QStringLiteral("[error] cannot create download path: %1")
                        .arg(QString::fromStdString(ec.message())));
        return false;
    }

    config_.download_path_ = abs.lexically_normal();

    // 下载路径在 SyncClient 构造时固定，改完必须整个重建；顺手把旧连接的会话断掉
    if (s_client_)
    {
        s_client_->disconnect();
        s_client_.reset();
    }
    client_servers_.clear();
    emit clientServersChanged();

    if (!persist())
    {
        return false;
    }

    setTip(QStringLiteral("[tip] download path -> %1")
               .arg(QString::fromStdString(config_.download_path_.string())));
    emit syncMessage(QStringLiteral("sync.path_changed"),
                     QString::fromStdString(config_.download_path_.generic_u8string()));
    emit syncStateChanged();
    return true;
}

bool ConfigBridge::addDownloadDirToLibrary()
{
    // "具体下载目录" = 下载根目录下的那一层任务目录（core 客户端 TaskReport 里的名字，
    // 也就是 ./download/<任务名>）；还没下载过就退回下载根目录本身
    const std::filesystem::path root = std::filesystem::u8path(config_.download_path_.string());
    std::error_code ec;
    std::filesystem::path target;
    QString source_desc;

    // 首选"上一个下载成功的任务目录"：core 客户端每完成一个入队目录会回一个 TaskReport，
    // 名字就是 downloadPath 下那一层目录（例：./download/音频）
    if (!last_task_dir_.isEmpty())
    {
        target = root / std::filesystem::u8path(last_task_dir_.toUtf8().toStdString());
        source_desc = QStringLiteral("last task dir");
    }

    if (target.empty() || !std::filesystem::is_directory(target, ec) || ec)
    {
        // 兜底（例如刚重启、还没有 TaskReport）：取下载根目录下**最近修改的子目录**，
        // 也就是最近一次下载落地的那层。
        // 注意：绝不退回下载根目录本身 —— 那会把 ./download 整个加进索引（里面还有 records.json）
        target.clear();
        source_desc = QStringLiteral("newest subdir of download path");

        std::filesystem::file_time_type newest{};
        std::error_code iter_ec;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(root, iter_ec))
        {
            std::error_code dir_ec;
            if (!entry.is_directory(dir_ec) || dir_ec)
            {
                continue;
            }

            const std::filesystem::file_time_type stamp = entry.last_write_time(dir_ec);
            if (dir_ec)
            {
                continue;
            }

            if (target.empty() || stamp > newest)
            {
                newest = stamp;
                target = entry.path();
            }
        }
    }

    if (target.empty())
    {
        reportError(QStringLiteral("[warning] no downloaded folder found under: %1")
                        .arg(QString::fromStdString(root.u8string())));
        emit syncMessage(QStringLiteral("sync.download_dir_unknown"), QString());
        return false;
    }

    logNow(QStringLiteral("[tip] addDownloadDirToLibrary picked %1: %2")
               .arg(source_desc)
               .arg(QString::fromStdString(target.generic_u8string()))
               .toStdString());

    const std::filesystem::path norm = std::filesystem::absolute(target, ec).lexically_normal();

    // 与「目录」页添加目录同一条路：白名单 + 索引
    if (!dm_.addDirectory(norm))
    {
        reportError(QString::fromUtf8(dm_.getLastError()));
        return false;
    }

    if (!ts_.addRoot(norm))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    dm_.saveToFile();

    setTip(QStringLiteral("[tip] download dir added to library: %1")
               .arg(QString::fromStdString(norm.generic_u8string())));
    emit syncMessage(QStringLiteral("sync.download_dir_added"),
                     QString::fromStdString(norm.generic_u8string()));
    emit libraryChanged();
    return true;
}
