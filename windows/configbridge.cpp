#include "configbridge.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

namespace
{

const char *MISSING_STRING_SENTINEL = "MISSING_STRING";
const char *ALL_ROWS_SENTINEL = "\001__tagmeow_list_all__";

// 正斜杠 + 去尾斜杠 Windows 下再统一小写 与 core 的路径归一化同一套规则
std::string genericKey(const std::filesystem::path &path)
{
    std::string key = path.generic_u8string();

    while (key.size() > 1 && key.back() == '/')
    {
        key.pop_back();
    }

#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
#endif

    return key;
}

// 路径统正斜杠的 UTF-8
QString pathToQString(const std::filesystem::path &path)
{
    return QString::fromStdString(path.generic_u8string());
}

// 浏览页文件行：图标种类 / 大小 / 时间 / 标签配色
// 这三套后缀表只影响"画哪个图标" 判定不了就算 plain
const QSet<QString> &imageSuffixes()
{
    static const QSet<QString> suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("ico"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("psd"), QStringLiteral("ai"),
        QStringLiteral("fig"), QStringLiteral("heic"), QStringLiteral("avif"), QStringLiteral("raw"),
        QStringLiteral("cr2"), QStringLiteral("nef")};
    return suffixes;
}

const QSet<QString> &docSuffixes()
{
    static const QSet<QString> suffixes = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("doc"),
        QStringLiteral("docx"), QStringLiteral("odt"), QStringLiteral("pdf"), QStringLiteral("rtf"),
        QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ods"), QStringLiteral("csv"),
        QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odp"), QStringLiteral("json"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("ini"), QStringLiteral("cfg"), QStringLiteral("conf"), QStringLiteral("log"),
        QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("c"), QStringLiteral("cpp"),
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("py"), QStringLiteral("js"),
        QStringLiteral("java"), QStringLiteral("cs"), QStringLiteral("go"), QStringLiteral("rs"),
        QStringLiteral("sh"), QStringLiteral("bat"), QStringLiteral("ps1")};
    return suffixes;
}

const QSet<QString> &videoSuffixes()
{
    static const QSet<QString> suffixes = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("webm"), QStringLiteral("m4v"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("rmvb"),
        QStringLiteral("3gp")};
    return suffixes;
}

QString iconKindOf(const QString &file_name)
{
    const int dot = file_name.lastIndexOf(QLatin1Char('.'));

    if (dot < 0 || dot == file_name.length() - 1)
    {
        return QStringLiteral("plain");
    }

    const QString suffix = file_name.mid(dot + 1).toLower();

    if (imageSuffixes().contains(suffix))
    {
        return QStringLiteral("img");
    }

    if (docSuffixes().contains(suffix))
    {
        return QStringLiteral("doc");
    }

    if (videoSuffixes().contains(suffix))
    {
        return QStringLiteral("video");
    }

    return QStringLiteral("plain");
}

QString sizeTextOf(qint64 bytes)
{
    if (bytes < 0)
    {
        return QStringLiteral("—");
    }

    const double value = static_cast<double>(bytes);

    if (bytes < 1024)
    {
        return QString::number(bytes) + QStringLiteral(" B");
    }

    if (bytes < 1024 * 1024)
    {
        return QString::number(static_cast<qint64>(value / 1024.0)) + QStringLiteral(" KB");
    }

    if (bytes < 1024LL * 1024LL * 1024LL)
    {
        return QString::number(value / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }

    return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB");
}

QString timeTextOf(const QDateTime &modified)
{
    if (!modified.isValid())
    {
        return QString();
    }

    const QDate day = modified.date();
    const QDate today = QDate::currentDate();
    const qint64 day_diff = day.daysTo(today);
    const QString clock = modified.toString(QStringLiteral("HH:mm"));

    if (day_diff == 0)
    {
        return QStringLiteral("今天 ") + clock;
    }

    if (day_diff == 1)
    {
        return QStringLiteral("昨天 ") + clock;
    }

    if (day_diff > 1 && day_diff < 7)
    {
        static const char *const week_names[] = {"一", "二", "三", "四", "五", "六", "日"};
        const int index = day.dayOfWeek() - 1;

        if (index >= 0 && index < 7)
        {
            return QStringLiteral("周") + QString::fromUtf8(week_names[index]) + QStringLiteral(" ") + clock;
        }

        return clock;
    }

    return day.toString(QStringLiteral("yyyy-MM-dd"));
}

// QML 侧的标签小片只有 blue / green / plain 三套配色（Chip.qml 没有"按类型色渲染"的入口）
// 用类型名的稳定哈希把类型映射到其中一套 保证同一类型的标签配色前后一致
QString toneOfType(const QString &type_name)
{
    const uint bucket = static_cast<uint>(qHash(type_name) % 3u);

    if (bucket == 0u)
    {
        return QStringLiteral("blue");
    }

    if (bucket == 1u)
    {
        return QStringLiteral("green");
    }

    return QStringLiteral("plain");
}
}

ConfigBridge::ConfigBridge(QObject *parent)
    : QObject(parent)
      // 构造里就读 ./config/config.json：文件不存在时 ConfigLoader 自己落一份默认值
    , config_()
      // 受管目录白名单 ./config/path.json（读取时会顺带算好每个目录的有效性）
    , dm_("./config/path.json")
      // 构造里就 loadLanguageList("./language")：扫目录拿到可选语言列表
    , language_("./language")
      // 标签库 + 索引库 根列表与存储模式都来自上面两个
    , ts_(dm_.getValidDirList(), config_.tag_mode_, "./config/tag.json", "./config/index.db")
    , last_error_()
      // 日志直接在本类里建
    , log_()
    , last_logged_()
{
    // 按配置里的 DefaultLanguage 加载一次字典 失败就当"没有 core 字典"
    // QML 侧的 Lang.qml 会自动落回自带的兜底表 界面不会因此空白
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

    // 构造函数里发信号没人接得住 排到事件循环第一次转起来之后再发
    // 那时 QML 早已建好 Store 的 Connections 启动期的警告也能弹成 Toast
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

    // 本类活得和应用一样久（QML 引擎持有的单例）析构里摘掉 不会留下悬空过滤器
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // 先取一次标签库快照：标签库页一加载就要渲染类型/标签列表
    refreshTagLibrary();

    // 工作线程的回调只发信号（syncStateChanged）—— 那些信号是跨线程发的 auto 连接会自动排队
    // 所以这里接的回调一定跑在主线程上：趁"有回报"的时候把队列/设备快照重新搬一遍
    // SyncServer::disconnect() 只是置位断开请求 由工作线程在下一个有界阻塞点真正清空队列
    // 所以断开后队列快照必须等这条回报才更新 不能在 disconnect() 之后立刻读
    connect(this, &ConfigBridge::syncStateChanged, this,
            [this]()
            {
                refreshServerQueue();

                // 状态一变就立刻落一条：下载的结局（成功 / 失败原因）是工作线程通过这个信号
                if (s_client_)
                {
                    logNow(QStringLiteral("[tip] sync state: downloading=%1 connected=%2 clientErr=[%3]")
                               .arg(clientDownloading() ? 1 : 0)
                               .arg(connectedServerIndex())
                               .arg(QString::fromStdString(s_client_->getLastError()))
                               .toStdString());
                }
            });

    // 任务(入队目录)完成回报
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

    // 标签库 / 索引一变 目录快照里的"文件数"和"有效性"就可能不一样了：
    // 只把快照置脏并发 dirsChanged 真正的重建等页面来读 dirs() 时再做（页面没开就一次都不做）
    // 同时：正处在层级浏览里的话 把当前层重扫一遍（标签写完/删完 行上的小片要跟着变）
    connect(this, &ConfigBridge::libraryChanged, this,
            [this]()
            {
                invalidateDirs();
                refreshLevel();
            });
}

ConfigBridge::~ConfigBridge()
{
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->removeEventFilter(this);
    }

    // 先停工作线程再销毁
    if (s_server_)
    {
        std::error_code ec;
        s_server_->stop(ec);
    }

    if (s_client_)
    {
        s_client_->disconnect();
    }
}

// 输入流截获
bool ConfigBridge::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (event != nullptr && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease))
    {
        QTimer::singleShot(0, this, [this]() { collectLogMessages(); });
    }

    return false;
}

// 写 log
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

// 立刻写盘：给同步这种异步流程的关键节点用
void ConfigBridge::logNow(const std::string &message)
{
    if (message.empty())
    {
        return;
    }

    log_.write("ConfigBridge", message);
    // 同步一下去重基线
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

    // 目录遍历顺序没有保证 排一下让设置页"循环切下一种语言"有确定次序
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
        reportError(QStringLiteral("[warning] FontSize out of range %1-%2: %3").arg(MIN_FONT_SIZE).arg(MAX_FONT_SIZE).arg(size));
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

    // 语言文件里的 name 字段可能与文件名主干不同 配置里存主干：下次启动 loadLanguage 才找得到文件
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

// 标签库 / 索引（core 的 DirectoryConfigManager + TagServe）

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
    // 直接查 core 的 out 参数存下来的那份数据 不解析任何字符串
    bool ok = false;
    const int number = last_values_.value(key, -1).toInt(&ok);
    return ok ? number : -1;
}

void ConfigBridge::setReport(const QVariantMap &values)
{
    last_values_ = values;

    // QVariantMap 按 key 排序遍历 拼出来的文本是稳定的
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
    const TagFileManager::StoreMode to_mode = (from_mode == TagFileManager::StoreMode::Sidecar) ? TagFileManager::StoreMode::Filename : TagFileManager::StoreMode::Sidecar;

    int converted = -1;
    int failed = -1;

    // keep_old = false：先整根写一遍新格式 全成功再回头删旧格式
    if (!ts_.convertMode(from_mode, to_mode, false, &converted, &failed))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        emit libraryChanged(); // 失败也可能已经改动过一部分文件 让界面重新取一次数
        return false;
    }

    // core 成功后已经把默认模式切成 to_mode 并重建了索引 把新模式同步进 config.json
    config_.tag_mode_ = to_mode;
    if (!persist())
    {
        return false;
    }

    // core 只回 bool 条目数一律走带 nullptr 默认值的 out 参数带出来 不解析它的 error_string_
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

    // reLoadRoot(全部有效目录)：内部先 clearAll 再按磁盘重扫 等价"重建索引"
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
    // 两步都只动数据库记录：cleanupInvalid 删掉磁盘上已不存在的记录 clearRepeat 去重
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


// 受管目录 目录页面

QVariantList ConfigBridge::dirs()
{
    // 懒重建：被置脏了才重算
    if (dirs_dirty_)
    {
        rebuildDirs();
    }

    return dirs_;
}

const Directory *ConfigBridge::dirAt(int index) const
{
    const std::vector<Directory> &all_dirs = dm_.getDirectories();

    if (index < 0 || static_cast<std::size_t>(index) >= all_dirs.size())
    {
        return nullptr;
    }

    return &all_dirs[static_cast<std::size_t>(index)];
}

void ConfigBridge::invalidateDirs()
{
    // 只置脏 + 发信号：页面下一轮读 dirs() 时才会真正重算
    dirs_dirty_ = true;
    emit dirsChanged();
}

void ConfigBridge::rebuildDirs()
{
    // 一次全表查询 在内存里按目录前缀统计每个根下的文件数（不受页面上的标签筛选影响）
    FileDatabase::SearchOptions options;
    options.exclude_.push_back(ALL_ROWS_SENTINEL);
    const std::vector<table::FileInfo> rows = ts_.searchByTags(options);

    std::vector<std::string> file_keys;
    file_keys.reserve(rows.size());

    for (const table::FileInfo &info : rows)
    {
        const std::filesystem::path path = std::filesystem::u8path(info.path_);
        std::error_code ec;

        if (std::filesystem::is_directory(path, ec) && !ec)
        {
            continue;
        }

        file_keys.push_back(genericKey(path));
    }

    dirs_.clear();

    for (const Directory &dir : dm_.getDirectories())
    {
        const std::string prefix = genericKey(dir.canonical_path_) + "/";
        int count = 0;

        for (const std::string &key : file_keys)
        {
            if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0)
            {
                count++;
            }
        }

        // path 只用于显示 增删改一律按下标走 所以用系统分隔符 读起来和资源管理器一致
        const QString display_path = QDir::toNativeSeparators(pathToQString(dir.canonical_path_));
        const QString leaf_name = QString::fromStdString(dir.canonical_path_.filename().u8string());

        QVariantMap item;
        item.insert(QStringLiteral("name"), leaf_name.isEmpty() ? display_path : leaf_name);
        item.insert(QStringLiteral("path"), display_path);
        item.insert(QStringLiteral("count"), count);
        item.insert(QStringLiteral("valid"), dir.is_valid_);
        dirs_.append(item);
    }

    dirs_dirty_ = false;
}

bool ConfigBridge::addDir(const QString &dir_url)
{
    const std::filesystem::path raw_path = localPathFromUrl(dir_url);

    if (raw_path.empty())
    {
        // 空路径处理
        emit libraryMessage(QStringLiteral("dir.add_requires_path"), QString());
        return false;
    }

    std::error_code ec;
    const std::filesystem::path norm_path = std::filesystem::absolute(raw_path, ec).lexically_normal();

    if (!std::filesystem::is_directory(norm_path, ec) || ec)
    {
        reportError(QStringLiteral("[warning] not a directory: %1").arg(dir_url));
        return false;
    }

    // 与「同步页 - 加入管理目录」同一条路：先加白名单 core 自己判重 / 判有效性 再加索引
    if (!dm_.addDirectory(norm_path))
    {
        reportError(QString::fromUtf8(dm_.getLastError()));
        return false;
    }

    // 加进白名单后用 core 规范化好的路径去建索引 两者必须是同一个写法否则根列表对不上
    const std::filesystem::path canonical_path = dm_.getLastValidDir();

    if (!ts_.addRoot(canonical_path))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        dm_.removeDirectory(canonical_path); // 回滚白名单
        return false;
    }

    dm_.saveToFile();

    const QString display_path = QDir::toNativeSeparators(pathToQString(canonical_path));
    setTip(QStringLiteral("[tip] dir added: %1").arg(display_path));
    emit libraryMessage(QStringLiteral("dir.added"), display_path);
    emit libraryChanged(); // -> invalidateDirs
    return true;
}

bool ConfigBridge::removeDir(int index)
{
    const Directory *dir = dirAt(index);

    if (dir == nullptr)
    {
        reportError(QStringLiteral("[warning] remove dir: index %1 out of range").arg(index));
        return false;
    }

    const std::filesystem::path dir_path = dir->canonical_path_;
    const QString display_path = QDir::toNativeSeparators(pathToQString(dir_path));

    // 先摘白名单 再删索引记录 只动配置与数据库
    if (!dm_.removeDirectory(dir_path))
    {
        reportError(QString::fromUtf8(dm_.getLastError()));
        return false;
    }

    if (!ts_.removeRoot(dir_path))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        dm_.addDirectory(dir_path); // 索引没删掉就把白名单加回去
        return false;
    }

    dm_.saveToFile();

    setTip(QStringLiteral("[tip] dir removed: %1").arg(display_path));
    emit libraryMessage(QStringLiteral("dir.removed_prefix"), display_path);
    emit libraryChanged(); // -> invalidateDirs
    return true;
}

bool ConfigBridge::refreshDir(int index)
{
    const Directory *dir = dirAt(index);

    if (dir == nullptr)
    {
        reportError(QStringLiteral("[warning] refresh dir: index %1 out of range").arg(index));
        return false;
    }

    const std::filesystem::path dir_path = dir->canonical_path_;
    const QString display_path = QDir::toNativeSeparators(pathToQString(dir_path));

    // 单目录刷新：只重扫这一个目录含子目录 不动其它根的记录 也不改根列表
    int indexed = -1;

    if (!ts_.reLoadRoot(dir_path, &indexed))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    QVariantMap report;
    report[QStringLiteral("indexed")] = indexed;
    setReport(report);

    setTip(QStringLiteral("[tip] dir refreshed: %1, %2").arg(display_path).arg(last_report_));
    emit libraryMessage(QStringLiteral("dir.refreshed"), display_path);
    emit libraryChanged(); // -> invalidateDirs
    return true;
}

bool ConfigBridge::revalidateDirs()
{
    const std::filesystem::path config_path = dm_.getConfigPath();
    std::error_code ec;
    bool ok = true;

    if (std::filesystem::exists(config_path, ec) && std::filesystem::file_size(config_path, ec) > 0)
    {
        ok = dm_.loadFromFile(config_path);
    }

    if (!ok && !dm_.getLastError().empty())
    {
        // 只记日志 界面照旧渲染上一次的快照
        setTip(QString::fromUtf8(dm_.getLastError()));
    }

    invalidateDirs();
    return ok;
}

bool ConfigBridge::openDir(int index)
{
    const Directory *dir = dirAt(index);

    if (dir == nullptr)
    {
        reportError(QStringLiteral("[warning] open dir: index %1 out of range").arg(index));
        return false;
    }

    // 「打开」= 直接进到该目录的层级浏览 桥把这层条目取好并发 browseEntriesChanged
    const QString dir_url = pathToQString(dir->canonical_path_);
    logNow(QStringLiteral("[tip] openDir -> browse: %1").arg(dir_url).toStdString());
    return enterDir(dir_url);
}

// 浏览页：查询与文件标签赋值
QVariantList ConfigBridge::browseFiles() const
{
    return browse_files_;
}

bool ConfigBridge::searching() const
{
    return searching_;
}

QVariantList ConfigBridge::buildBrowseRows(const std::vector<table::FileInfo> &rows) const
{
    QVariantList result;
    result.reserve(static_cast<int>(rows.size()));

    for (const table::FileInfo &info : rows)
    {
        const QVariantMap item = makeBrowseRow(info, true);

        if (!item.isEmpty())
        {
            result.append(item);
        }
    }

    return result;
}

QVariantMap ConfigBridge::makeBrowseRow(const table::FileInfo &info, bool keep_dirs) const
{
    const std::filesystem::path path = std::filesystem::u8path(info.path_);
    std::error_code ec;
    const bool is_dir = std::filesystem::is_directory(path, ec) && !ec;

    // core 的 insertDirectory 会把子目录也写成记录：
    // 平铺结果里只留文件 这一版平铺就是文件列表 层级模式里目录要单独成行
    if (is_dir && !keep_dirs)
    {
        return QVariantMap();
    }

    const QString full_path = pathToQString(path);
    const QFileInfo file_info(full_path);
    const QString file_name = file_info.fileName().isEmpty() ? full_path : file_info.fileName();
    const QString icon_kind = is_dir ? QStringLiteral("dir") : iconKindOf(file_name);

    QVariantList tag_items;
    tag_items.reserve(static_cast<int>(info.tags_.size()));

    for (const std::string &tag : info.tags_)
    {
        const QString tag_name = QString::fromUtf8(tag);
        const QString type_name = QString::fromUtf8(ts_.getTypeOfTag(tag));

        QVariantMap tag_item;
        tag_item.insert(QStringLiteral("name"), tag_name);
        tag_item.insert(QStringLiteral("tone"), toneOfType(type_name));
        tag_items.append(tag_item);
    }

    QVariantMap item;
    item.insert(QStringLiteral("fileName"), file_name);
    // 只用于显示 -> 系统分隔符 读起来和资源管理器一致
    item.insert(QStringLiteral("dirPath"), QDir::toNativeSeparators(pathToQString(path.parent_path())));
    item.insert(QStringLiteral("sizeText"), is_dir ? QString() : (file_info.exists() ? sizeTextOf(file_info.size()) : QStringLiteral("—")));
    item.insert(QStringLiteral("timeText"), file_info.exists() ? timeTextOf(file_info.lastModified()) : QString());
    item.insert(QStringLiteral("iconKind"), icon_kind);
    item.insert(QStringLiteral("tags"), tag_items);
    item.insert(QStringLiteral("path"), full_path);
    item.insert(QStringLiteral("kind"), is_dir ? QStringLiteral("dir") : QStringLiteral("file"));
    return item;
}

bool ConfigBridge::search(const QStringList &include, const QStringList &exclude,
                          const QStringList &only, const QStringList &dirs)
{
    // 只记条件：同一次事件循环里连点搜索也只会真正查最后一次
    last_include_ = include;
    last_exclude_ = exclude;
    last_only_ = only;
    last_dirs_ = dirs;
    has_searched_ = true;

    // core 的 searchByTags 是同步的（跑完才回来），所以先亮"搜索中"，把查询排到下一轮事件循环：
    // 界面才有机会把加载态画出来，而不是整窗先卡一下
    if (!searching_)
    {
        searching_ = true;
        emit searchStateChanged();
    }

    if (!search_pending_)
    {
        search_pending_ = true;
        QTimer::singleShot(0, this, [this]() { runPendingSearch(); });
    }

    return true;
}

bool ConfigBridge::runPendingSearch()
{
    if (!search_pending_)
    {
        return false;
    }

    search_pending_ = false;

    FileDatabase::SearchOptions options;

    for (const QString &tag : last_include_)
    {
        if (!tag.isEmpty())
        {
            options.include_.push_back(tag.toUtf8().toStdString());
        }
    }

    for (const QString &tag : last_exclude_)
    {
        if (!tag.isEmpty())
        {
            options.exclude_.push_back(tag.toUtf8().toStdString());
        }
    }

    for (const QString &tag : last_only_)
    {
        if (!tag.isEmpty())
        {
            options.only_.push_back(tag.toUtf8().toStdString());
        }
    }

    for (const QString &dir : last_dirs_)
    {
        if (dir.isEmpty())
        {
            continue;
        }

        options.dirs_.push_back(std::filesystem::u8path(dir.toUtf8().toStdString()));
    }

    // 只把 core 的记录留下来 "喂到哪一段才拼哪一段"
    // 顺序就是 core 给的顺序
    pending_infos_ = ts_.searchByTags(options);
    pending_row_offset_ = 0;
    browse_files_.clear();

    // 三个标签容器都为空时 core 的语义是"返回没有标签的文件" 不是错误
    // 只有真的查不动了（DB 报错）才打扰用户
    const std::string db_error = ts_.getDBError();

    if (pending_infos_.empty() && !db_error.empty() && db_error.compare(0, 5, "[tip]") != 0)
    {
        reportError(QString::fromUtf8(db_error));
    }

    setTip(QStringLiteral("[tip] search include=%1 exclude=%2 only=%3 dirs=%4 -> %5").arg(last_include_.size()).arg(last_only_.size()).arg(last_dirs_.size()).arg(pending_infos_.size()));

    // 先把窗口清空（加载态盖在上面）随后拼并喂第一段
    emit browseChanged();
    feedBrowseBatch();
    return true;
}

int ConfigBridge::browseTotal() const
{
    return static_cast<int>(pending_infos_.size());
}

void ConfigBridge::feedBrowseBatch()
{
    // 界面只渲染一个范围：拼好下一段 BROWSE_PAGE_SIZE 行交给界面（只有这一段会去 stat 磁盘）
    const int total = static_cast<int>(pending_infos_.size());
    const int limit = pending_row_offset_ + BROWSE_PAGE_SIZE;
    const int end = total < limit ? total : limit;

    while (pending_row_offset_ < end)
    {
        const QVariantMap row = makeBrowseRow(pending_infos_[static_cast<std::size_t>(pending_row_offset_)], true);

        if (!row.isEmpty())
        {
            browse_files_.append(row);
        }

        pending_row_offset_++;
    }

    emit browseChanged();

    // 首屏一出去就把"正在搜索"收掉：searching 只表示"正在查"
    if (searching_)
    {
        searching_ = false;
        emit searchStateChanged();
    }

    if (pending_row_offset_ < total)
    {
        // 还有剩下的：等界面滚到底再来取
        return;
    }

    logNow(QStringLiteral("[tip] browse window ready: %1 of %2 row(s)")
               .arg(browse_files_.size())
               .arg(total)
               .toStdString());
}

bool ConfigBridge::loadMoreRows()
{
    if (pending_row_offset_ >= static_cast<int>(pending_infos_.size()))
    {
        return false;
    }

    feedBrowseBatch();
    return true;
}

void ConfigBridge::patchBrowseRow(const QString &file_path, const QString &real_path)
{
    // 标签写完只改这一行：从 core 把这条记录重读出来重新拼一行替换掉 在层级里就重扫当前这一层
    if (!level_path_.empty())
    {
        refreshLevel();
        return;
    }

    if (file_path.isEmpty())
    {
        return;
    }

    for (int i = 0; i < browse_files_.size(); i++)
    {
        if (browse_files_.at(i).toMap().value(QStringLiteral("path")).toString() != file_path)
        {
            continue;
        }

        const QString lookup = real_path.isEmpty() ? file_path : real_path;
        const std::optional<table::FileInfo> info = ts_.getFileInfo(localPathFromUrl(lookup));

        if (!info.has_value())
        {
            // 记录已经不在索引里了：把这一行从窗口里去掉
            browse_files_.removeAt(i);
            emit browseChanged();
            return;
        }
        
        if (info.has_value())
        {
            const QVariantMap row = makeBrowseRow(*info, true);

            if (!row.isEmpty())
            {
                browse_files_[i] = row;
            }
        }

        emit browseChanged();
        return;
    }

    // 这一行不在当前窗口里（还没滚到 / 已被筛掉）：什么都不用做 下次搜索自然会一致
}

void ConfigBridge::refreshBrowseView()
{
    // 层级浏览只按目录渲染
    if (!level_path_.empty())
    {
        refreshLevel();
        return;
    }

    rerunLastSearch();
}

bool ConfigBridge::rerunLastSearch()
{
    if (!has_searched_)
    {
        return false;
    }

    return search(last_include_, last_exclude_, last_only_, last_dirs_);
}

// 浏览页：层级浏览 不平铺 和平铺共用同一套行渲染

QVariantList ConfigBridge::browseEntries() const
{
    return browse_entries_;
}

QString ConfigBridge::browseLevel() const
{
    return level_path_.empty() ? QString() : pathToQString(level_path_);
}

QVariantList ConfigBridge::buildLevelEntries(const std::filesystem::path &dir) const
{
    QVariantList result;

    // 第一条固定是"返回上一层"（名字 "."）：只有上一层还在受管目录里才给 最上层就没有这条
    if (!level_parent_.empty())
    {
        QVariantMap parent_item;
        parent_item.insert(QStringLiteral("fileName"), QStringLiteral("."));
        parent_item.insert(QStringLiteral("dirPath"), QDir::toNativeSeparators(pathToQString(level_parent_)));
        parent_item.insert(QStringLiteral("sizeText"), QString());
        parent_item.insert(QStringLiteral("timeText"), QString());
        parent_item.insert(QStringLiteral("iconKind"), QStringLiteral("dir"));
        parent_item.insert(QStringLiteral("tags"), QVariantList());
        parent_item.insert(QStringLiteral("path"), pathToQString(level_parent_));
        parent_item.insert(QStringLiteral("kind"), QStringLiteral("parent"));
        result.append(parent_item);
    }

    // 这一层的直接子项：一律从 core 的索引里取（数据源以 core 为准 不直接翻磁盘）
    FileDatabase::SearchOptions options;
    options.exclude_.push_back(ALL_ROWS_SENTINEL);
    options.dirs_.push_back(dir);

    const std::vector<table::FileInfo> rows = ts_.searchByTags(options);
    const std::string prefix = genericKey(dir) + "/";

    QVariantList dir_rows;
    QVariantList file_rows;

    for (const table::FileInfo &info : rows)
    {
        const std::filesystem::path path = std::filesystem::u8path(info.path_);
        const std::string key = genericKey(path);

        if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
        {
            continue;
        }

        if (key.find('/', prefix.size()) != std::string::npos)
        {
            continue;
        }

        const QVariantMap item = makeBrowseRow(info, true);

        if (item.isEmpty())
        {
            continue;
        }

        if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("dir"))
        {
            dir_rows.append(item);
        }
        else
        {
            file_rows.append(item);
        }
    }

    auto by_name = [](const QVariantList &list)
    {
        QStringList names;
        names.reserve(list.size());

        for (const QVariant &entry : list)
        {
            names.append(entry.toMap().value(QStringLiteral("fileName")).toString());
        }

        QList<int> order;
        order.reserve(list.size());

        for (int i = 0; i < list.size(); i++)
        {
            order.append(i);
        }

        std::stable_sort(order.begin(), order.end(), [&names](int left, int right)
        {
            return QString::localeAwareCompare(names.at(left), names.at(right)) < 0;
        });

        QVariantList sorted;
        sorted.reserve(list.size());

        for (int index : order)
        {
            sorted.append(list.at(index));
        }

        return sorted;
    };

    result.append(by_name(dir_rows));
    result.append(by_name(file_rows));
    return result;
}

bool ConfigBridge::enterLevel(const std::filesystem::path &dir)
{
    level_path_ = dir;

    std::error_code ec;
    const std::filesystem::path parent = dir.parent_path();
    level_parent_.clear();

    if (!parent.empty() && parent != dir && dm_.isPathAllowed(parent))
    {
        level_parent_ = parent;
    }

    browse_entries_ = buildLevelEntries(dir);
    emit browseEntriesChanged();
    return true;
}

bool ConfigBridge::enterDir(const QString &dir_path)
{
    if (dir_path.isEmpty())
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::absolute(localPathFromUrl(dir_path), ec).lexically_normal();

    if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec)
    {
        reportError(QStringLiteral("[warning] not a directory: %1").arg(dir_path));
        return false;
    }

    setTip(QStringLiteral("[tip] enter dir: %1").arg(pathToQString(dir)));
    return enterLevel(dir);
}

void ConfigBridge::exitLevel()
{
    if (level_path_.empty())
    {
        return;
    }

    level_path_.clear();
    level_parent_.clear();
    browse_entries_.clear();
    emit browseEntriesChanged();
}

bool ConfigBridge::goUpLevel()
{
    if (level_parent_.empty())
    {
        return false;
    }

    const std::filesystem::path parent = level_parent_;
    return enterLevel(parent);
}

void ConfigBridge::refreshLevel()
{
    if (level_path_.empty())
    {
        return;
    }

    browse_entries_ = buildLevelEntries(level_path_);
    emit browseEntriesChanged();
}

bool ConfigBridge::openEntryInSystem(const QString &path)
{
    const std::filesystem::path target = localPathFromUrl(path);

    if (target.empty())
    {
        return false;
    }

    const QFileInfo info(pathToQString(target));

    if (!info.exists())
    {
        reportError(QStringLiteral("[warning] path does not exist: %1").arg(pathToQString(target)));
        return false;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

bool ConfigBridge::revealInSystem(const QString &path)
{
    const std::filesystem::path target = localPathFromUrl(path);

    if (target.empty())
    {
        return false;
    }

    const QFileInfo info(pathToQString(target));

    if (!info.exists())
    {
        reportError(QStringLiteral("[warning] path does not exist: %1").arg(pathToQString(target)));
        return false;
    }

    const QString absolute_path = info.absoluteFilePath();

#ifdef Q_OS_WIN
    // 文件 -> 打开所在目录并选中它 目录 -> 直接打开该目录
    const std::wstring native_path = QDir::toNativeSeparators(absolute_path).toStdWString();
    PIDLIST_ABSOLUTE item_id = ILCreateFromPathW(native_path.c_str());

    if (item_id != nullptr)
    {
        const HRESULT result = SHOpenFolderAndSelectItems(item_id, 0, nullptr, 0);
        ILFree(item_id);

        if (SUCCEEDED(result))
        {
            return true;
        }
    }

    // shell 调用失败（或不是 Windows）就退回"打开所在目录"
#endif
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? absolute_path : info.absolutePath()));
}

bool ConfigBridge::assignTagToFile(const QString &file_path, const QString &tag)
{
    const QString trimmed_tag = tag.trimmed();

    if (file_path.isEmpty() || trimmed_tag.isEmpty())
    {
        return false;
    }

    const std::filesystem::path path = localPathFromUrl(file_path);

    if (!ts_.addFileTag(path, trimmed_tag.toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    const QString real_path = pathToQString(ts_.getLastFilePath());
    logNow(QStringLiteral("[tip] assign tag %1 -> %2").arg(trimmed_tag).arg(real_path).toStdString());

    // Filename 模式下加/删标签会把文件改名：库里的记录跟着换成**新路径**，
    const std::filesystem::path last_path = ts_.getLastFilePath();

    if (!real_path.isEmpty() && real_path != pathToQString(path))
    {
        ts_.removeFile(path);
        ts_.updateFile(last_path);
        logNow(QStringLiteral("[tip] filename mode rename synced: %1 -> %2")
                   .arg(pathToQString(path))
                   .arg(real_path)
                   .toStdString());
    }

    patchBrowseRow(file_path, real_path);
    return true;
}

bool ConfigBridge::removeTagFromFile(const QString &file_path, const QString &tag)
{
    const QString trimmed_tag = tag.trimmed();

    if (file_path.isEmpty() || trimmed_tag.isEmpty())
    {
        return false;
    }

    const std::filesystem::path path = localPathFromUrl(file_path);

    if (!ts_.removeFileTag(path, trimmed_tag.toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    logNow(QStringLiteral("[tip] remove tag %1 <- %2").arg(trimmed_tag).arg(pathToQString(path)).toStdString());

    // Filename 模式下加/删标签会把文件改名：库里的记录跟着换成新路径
    const std::filesystem::path last_path = ts_.getLastFilePath();
    const QString real_path = last_path.empty() ? QString() : pathToQString(last_path);

    if (!real_path.isEmpty() && real_path != pathToQString(path))
    {
        ts_.removeFile(path);
        ts_.updateFile(last_path);
        logNow(QStringLiteral("[tip] filename mode rename synced: %1 -> %2").arg(pathToQString(path)).arg(real_path).toStdString());
    }

    patchBrowseRow(file_path, real_path);
    return true;
}

bool ConfigBridge::removeFileFromIndex(const QString &file_path)
{
    if (file_path.isEmpty())
    {
        return false;
    }

    const std::filesystem::path path = localPathFromUrl(file_path);

    if (!ts_.removeFile(path))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    setTip(QStringLiteral("[tip] file removed from index: %1").arg(pathToQString(path)));
    patchBrowseRow(file_path);
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

    // 成功回报：导出了多少字节 这里按复制后的实际大小报
    std::error_code size_ec;
    const auto exported_bytes = std::filesystem::file_size(target_path, size_ec);

    QVariantMap report;
    report[QStringLiteral("bytes")] = size_ec ? 0 : static_cast<qulonglong>(exported_bytes);
    setReport(report);

    setTip(QStringLiteral("[tip] tag library exported to %1, %2").arg(QString::fromStdString(target_path.u8string())).arg(last_report_));
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

    // mergeTags 内部合并成功后立刻写盘（类型 / 标签全局唯一 只补本库没有的）
    if (!ts_.mergeTags(source_path, &types, &tags))
    {
        reportError(QString::fromUtf8(ts_.getLastError()));
        return false;
    }

    QVariantMap report;
    report[QStringLiteral("types")] = types;
    report[QStringLiteral("tags")] = tags;
    setReport(report);

    setTip(QStringLiteral("[tip] tag library imported from %1, %2").arg(QString::fromStdString(source_path.u8string())).arg(last_report_));

    // 合并会改标签库 -> 界面上的类型/标签列表跟着刷新
    refreshTagLibrary();
    emit libraryChanged();
    return true;
}


// 标签库：类型 / 标签
QVariantList ConfigBridge::types() const
{
    return types_;
}

QVariantList ConfigBridge::tags() const
{
    return tags_;
}

QString ConfigBridge::typeNameOfId(int type_id) const
{
    for (auto it = type_ids_.constBegin(); it != type_ids_.constEnd(); ++it)
    {
        if (it.value() == type_id)
        {
            return it.key();
        }
    }

    return QString();
}

QString ConfigBridge::tagNameOfId(int tag_id) const
{
    for (auto it = tag_ids_.constBegin(); it != tag_ids_.constEnd(); ++it)
    {
        if (it.value() == tag_id)
        {
            return it.key();
        }
    }

    return QString();
}

void ConfigBridge::refreshTagLibrary()
{
    const std::unordered_map<std::string, std::vector<std::string>> type_tags = ts_.getTypeTag();
    const std::unordered_map<std::string, std::string> type_colors = ts_.getTypeColor();

    // core 用的是 unordered_map 遍历顺序不稳定 -> 先排序 保证界面上的行序固定
    std::vector<std::string> names;
    names.reserve(type_tags.size());
    for (const auto &entry : type_tags)
    {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());

    QHash<QString, bool> alive_types;
    QHash<QString, bool> alive_tags;

    types_.clear();
    tags_.clear();

    for (const std::string &name : names)
    {
        const QString type_name = QString::fromUtf8(name);
        alive_types.insert(type_name, true);

        // id 一旦分配就跟着名字走：刷新（增删之后）不会让已经选中的那一项指到别的类型上
        if (!type_ids_.contains(type_name))
        {
            type_ids_.insert(type_name, next_type_id_);
            next_type_id_++;
        }
        const int type_id = type_ids_.value(type_name);

        QVariantMap type_item;
        type_item.insert(QStringLiteral("typeId"), type_id);
        type_item.insert(QStringLiteral("name"), type_name);

        auto color_it = type_colors.find(name);
        type_item.insert(QStringLiteral("color"),color_it == type_colors.end() ? QString() : QString::fromUtf8(color_it->second));
        types_.append(type_item);

        auto tags_it = type_tags.find(name);
        if (tags_it == type_tags.end())
        {
            continue;
        }

        for (const std::string &tag : tags_it->second)
        {
            const QString tag_name = QString::fromUtf8(tag);
            alive_tags.insert(tag_name, true);

            if (!tag_ids_.contains(tag_name))
            {
                tag_ids_.insert(tag_name, next_tag_id_);
                next_tag_id_++;
            }

            QVariantMap tag_item;
            tag_item.insert(QStringLiteral("tagId"), tag_ids_.value(tag_name));
            tag_item.insert(QStringLiteral("typeId"), type_id);
            tag_item.insert(QStringLiteral("name"), tag_name);
            tags_.append(tag_item);
        }
    }

    // 回收已经不存在的名字
    for (auto it = type_ids_.begin(); it != type_ids_.end();)
    {
        it = alive_types.contains(it.key()) ? std::next(it) : type_ids_.erase(it);
    }

    for (auto it = tag_ids_.begin(); it != tag_ids_.end();)
    {
        it = alive_tags.contains(it.key()) ? std::next(it) : tag_ids_.erase(it);
    }

    emit typesChanged();
    emit tagsChanged();
}

bool ConfigBridge::addType(const QString &type_name, const QString &color)
{
    const QString name = type_name.trimmed();
    if (name.isEmpty())
    {
        reportError(QStringLiteral("[warning] addType rejected: empty type name"));
        emit libraryMessage(QStringLiteral("editor.name_required"), QString());
        return false;
    }

    const std::string name_utf8 = name.toUtf8().toStdString();

    // 同名类型已存在 ->「重设颜色」
    const std::unordered_map<std::string, std::string> existing = ts_.getTypeColor();
    if (existing.find(name_utf8) != existing.end())
    {
        if (!setTypeColor(type_ids_.value(name, -1), color))
        {
            return false;
        }

        logNow(QStringLiteral("[tip] addType: name already exists, color reset: %1").arg(name).toStdString());
        emit libraryMessage(QStringLiteral("tag.type_color_reset"), name);
        return true;
    }

    std::string color_utf8 = color.trimmed().toUtf8().toStdString();
    if (!ts_.addType(name_utf8, color_utf8))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        return false;
    }

    ts_.saveTag();
    refreshTagLibrary();

    setTip(QStringLiteral("[tip] type added: %1 color=%2").arg(name).arg(QString::fromStdString(color_utf8)));
    if (QString::fromStdString(color_utf8).compare(color.trimmed(), Qt::CaseInsensitive) != 0)
    {
        emit libraryMessage(QStringLiteral("tag.type_added_color_fixed"), name);
    }
    else
    {
        emit libraryMessage(QStringLiteral("tag.type_added"), name);
    }

    emit libraryChanged();
    return true;
}

bool ConfigBridge::removeType(int type_id)
{
    const QString name = typeNameOfId(type_id);
    if (name.isEmpty())
    {
        reportError(QStringLiteral("[warning] removeType rejected: unknown type id %1").arg(type_id));
        return false;
    }

    if (!ts_.removeType(name.toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        return false;
    }

    ts_.saveTag();
    refreshTagLibrary();

    setTip(QStringLiteral("[tip] type removed: %1").arg(name));
    emit libraryMessage(QStringLiteral("tag.type_removed"), name);
    emit libraryChanged();
    return true;
}

bool ConfigBridge::renameType(const QString &old_name, const QString &new_name)
{
    const QString old_trimmed = old_name.trimmed();
    const QString new_trimmed = new_name.trimmed();

    if (old_trimmed.isEmpty())
    {
        reportError(QStringLiteral("[warning] renameType rejected: empty source name"));
        emit libraryMessage(QStringLiteral("tag.pick_type_first"), QString());
        return false;
    }

    if (new_trimmed.isEmpty())
    {
        reportError(QStringLiteral("[warning] renameType rejected: empty new name"));
        emit libraryMessage(QStringLiteral("editor.name_required"), QString());
        return false;
    }

    const std::unordered_map<std::string, std::string> existing = ts_.getTypeColor();
    if (existing.find(old_trimmed.toUtf8().toStdString()) == existing.end())
    {
        reportError(QStringLiteral("[warning] renameType rejected: type not found: %1").arg(old_trimmed));
        emit libraryMessage(QStringLiteral("tag.pick_type_first"), QString());
        return false;
    }

    // 先把 id 记下来：core 的标签库按名字索引 改名之后旧名字消失新名字出现
    const int kept_id = type_ids_.value(old_trimmed, -1);

    if (!ts_.renameType(old_trimmed.toUtf8().toStdString(), new_trimmed.toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        emit libraryMessage(QStringLiteral("tag.rename_failed"), old_trimmed);
        return false;
    }

    ts_.saveTag();

    type_ids_.remove(old_trimmed);
    if (kept_id > 0)
    {
        type_ids_.insert(new_trimmed, kept_id);
    }

    refreshTagLibrary();

    setTip(QStringLiteral("[tip] type renamed: %1 -> %2").arg(old_trimmed).arg(new_trimmed));
    emit libraryMessage(QStringLiteral("tag.type_renamed"), new_trimmed);
    emit libraryChanged();
    return true;
}

bool ConfigBridge::setTypeColor(int type_id, const QString &color)
{
    const QString name = typeNameOfId(type_id);
    if (name.isEmpty())
    {
        reportError(QStringLiteral("[warning] setTypeColor rejected: unknown type id %1").arg(type_id));
        return false;
    }

    if (!ts_.setTypeColor(name.toUtf8().toStdString(), color.trimmed().toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        emit libraryMessage(QStringLiteral("tag.color_failed"), name);
        return false;
    }

    ts_.saveTag();
    refreshTagLibrary();

    setTip(QStringLiteral("[tip] type color set: %1 -> %2").arg(name).arg(color.trimmed()));
    emit libraryMessage(QStringLiteral("tag.color_updated"), name);
    emit libraryChanged();
    return true;
}

bool ConfigBridge::addTag(int type_id, const QString &tag_name)
{
    const QString type_name = typeNameOfId(type_id);
    const QString trimmed = tag_name.trimmed();

    if (type_name.isEmpty())
    {
        reportError(QStringLiteral("[warning] addTag rejected: unknown type id %1").arg(type_id));
        emit libraryMessage(QStringLiteral("tag.pick_type_first"), QString());
        return false;
    }

    if (trimmed.isEmpty())
    {
        reportError(QStringLiteral("[warning] addTag rejected: empty tag name"));
        emit libraryMessage(QStringLiteral("editor.name_required"), QString());
        return false;
    }

    // 标签全局唯一：已存在同名标签时 core 是静默 no-op 返回 true 但什么都不做
    const std::string tag_utf8 = trimmed.toUtf8().toStdString();
    const std::unordered_map<std::string, std::vector<std::string>> all = ts_.getTypeTag();
    for (const auto &entry : all)
    {
        const std::vector<std::string> &vec = entry.second;
        if (std::find(vec.begin(), vec.end(), tag_utf8) != vec.end())
        {
            emit libraryMessage(QStringLiteral("tag.tag_exists"), trimmed);
            emit libraryChanged();
            return true;
        }
    }

    if (!ts_.addTag(type_name.toUtf8().toStdString(), tag_utf8))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        return false;
    }

    ts_.saveTag();
    refreshTagLibrary();

    setTip(QStringLiteral("[tip] tag added: %1 -> %2").arg(trimmed).arg(type_name));
    emit libraryMessage(QStringLiteral("tag.tag_added"), trimmed);
    emit libraryChanged();
    return true;
}

bool ConfigBridge::removeTag(int tag_id)
{
    const QString name = tagNameOfId(tag_id);
    if (name.isEmpty())
    {
        reportError(QStringLiteral("[warning] removeTag rejected: unknown tag id %1").arg(tag_id));
        return false;
    }

    if (!ts_.removeTag(name.toUtf8().toStdString()))
    {
        reportError(QString::fromUtf8(ts_.getTagError()));
        return false;
    }

    ts_.saveTag();
    refreshTagLibrary();

    setTip(QStringLiteral("[tip] tag removed: %1").arg(name));
    emit libraryMessage(QStringLiteral("tag.tag_removed"), name);
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

    // 也接受直接给的本地路径
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
    last_error_ = message;
}


// 同步：服务端
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

    // 广播端口 / 魔术字 / 空队列等待时长都来自 config.json
    s_server_ = std::make_unique<SyncServer>(config_.broadcast_port_, config_.broadcast_magic_word_, config_.server_waiting_time_);
    s_server_->setTaskCallback([this](const TaskReport &report)
                               {
                                   // 工作线程：只发信号（Qt 会自动排队到主线程）不碰任何成员
                                   emit syncMessage(QStringLiteral("sync.task_done"), QString::fromStdString(report.name_));
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
    logNow(QStringLiteral("[tip] sync toggleServer entered: name=[%1] running=%2").arg(server_name).arg(server_running_flag_ ? 1 : 0).toStdString());

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
        // 名字留空就兜底成默认名
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
                                             emit syncMessage(queue_empty ? QStringLiteral("sync.server_waiting") : QStringLiteral("sync.server_session_end"), QString());
                                         }
                                         else
                                         {
                                             emit syncMessage(QStringLiteral("sync.server_session_error"), QString());
                                         }
                                         emit syncStateChanged();
                                     });

    if (!ok)
    {
        reportError(QStringLiteral("[error] sync server start failed: %1").arg(QString::fromStdString(ec.message())));
        emit syncMessage(QStringLiteral("sync.start_failed"), QString::fromStdString(ec.message()));
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
    logNow(QStringLiteral("[tip] sync enqueueServerDir entered: [%1] running=%2").arg(dir_path).arg(server_running_flag_ ? 1 : 0).toStdString());

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

    // 入队后立刻把队列快照推给界面
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

// 同步：客户端
bool ConfigBridge::clientDownloading() const
{
    return s_client_ && s_client_->isDownloading();
}

int ConfigBridge::connectedServerIndex() const
{
    // 只在"正在连接/下载"期间才报下标：会话一结束就回到 -1
    // 这样界面上的灰化会跟着会话自动恢复（is_busy_ 由 SyncClient 原子维护 跨线程读安全）
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

    // 保存目录固定取 config.json 的 DownloadPath（默认 ./download）
    // 想改只能整个重建（core 的 SyncClient 构造时固定下载路径）
    s_client_ = std::make_unique<SyncClient>(config_.broadcast_port_, config_.broadcast_magic_word_, std::filesystem::u8path(config_.download_path_.string()));
    s_client_->setTaskCallback([this](const TaskReport &report)
                               {
                                   // 工作线程：只发信号 排队回主线程后再记 last_task_dir_
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

    const std::vector<ServerInfo> servers = s_client_->scanServers();
    refreshClientServers();

    for (const ServerInfo &s : servers)
    {
        logNow(QStringLiteral("[tip] sync scan hit: name=%1 ip=%2 port=%3").arg(QString::fromStdString(s.name_)).arg(QString::fromStdString(s.ip_)).arg(s.port_).toStdString());
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
    logNow(QStringLiteral("[tip] sync startDownload idx=%1 servers=%2 downloading=%3").arg(servers.size()).arg(clientDownloading() ? 1 : 0).toStdString());

    if (server_index < 0 || static_cast<std::size_t>(server_index) >= servers.size())
    {
        reportError(QStringLiteral("[warning] download rejected: no valid device selected, index %1").arg(server_index));
        emit syncMessage(QStringLiteral("sync.no_selection"), QString());
        return false;
    }

    const QString device_name = QString::fromStdString(servers[static_cast<std::size_t>(server_index)].name_);
    emit syncMessage(QStringLiteral("sync.downloading"), device_name);

    connected_server_index_ = server_index;
    s_client_->startDownload(static_cast<std::size_t>(server_index),
                             [this](bool success, std::error_code e)
                             {
                                 // 工作线程回调：只发信号
                                 if (success)
                                 {
                                     const bool queue_empty = (e == std::make_error_code(std::errc::no_message_available));
                                     emit syncMessage(queue_empty ? QStringLiteral("sync.download_empty") : QStringLiteral("sync.download_done"), QString());
                                 }
                                 else
                                 {
                                     emit syncMessage(QStringLiteral("sync.download_failed"), QString::fromStdString(s_client_ ? s_client_->getLastError() : std::string()));
                                 }
                                 emit syncStateChanged();
                             });

    // 顺序很重要：SyncClient::startDownload 会在返回前**同步**把 is_busy_ 置位
    // 所以这个信号必须发在它之后 —— 发在前面的话 QML 读 clientDownloading 还是 false
    // 下载按钮就不会变灰 而下一个状态信号要等会话结束才来（整段下载期间都不置灰）
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

    // 下载路径在 SyncClient 构造时固定 改完必须整个重建 顺手把旧连接的会话断掉
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

    setTip(QStringLiteral("[tip] download path -> %1").arg(QString::fromStdString(config_.download_path_.string())));
    emit syncMessage(QStringLiteral("sync.path_changed"), QString::fromStdString(config_.download_path_.generic_u8string()));
    emit syncStateChanged();
    return true;
}

bool ConfigBridge::addDownloadDirToLibrary()
{
    // "具体下载目录" = 下载根目录下的那一层任务目录（core 客户端 TaskReport 里的名字
    // 也就是 ./download/<任务名>）还没下载过就退回下载根目录本身
    const std::filesystem::path root = std::filesystem::u8path(config_.download_path_.string());
    std::error_code ec;
    std::filesystem::path target;
    QString source_desc;

    // 名字就是 downloadPath 下那一层目录
    if (!last_task_dir_.isEmpty())
    {
        target = root / std::filesystem::u8path(last_task_dir_.toUtf8().toStdString());
        source_desc = QStringLiteral("last task dir");
    }

    if (target.empty() || !std::filesystem::is_directory(target, ec) || ec)
    {
        // 兜底（例如刚重启 还没有 TaskReport）：取下载根目录下 最近修改的子目录 也就是最近一次下载落地的那层
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
        reportError(QStringLiteral("[warning] no downloaded folder found under: %1").arg(QString::fromStdString(root.u8string())));
        emit syncMessage(QStringLiteral("sync.download_dir_unknown"), QString());
        return false;
    }

    logNow(QStringLiteral("[tip] addDownloadDirToLibrary picked %1: %2").arg(source_desc).arg(QString::fromStdString(target.generic_u8string())).toStdString());
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
