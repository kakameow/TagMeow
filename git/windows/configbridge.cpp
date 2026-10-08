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
}

ConfigBridge::~ConfigBridge()
{
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->removeEventFilter(this);
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
