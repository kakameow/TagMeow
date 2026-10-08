#ifndef CONFIG_BRIDGE_H
#define CONFIG_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "config_loader.h"
#include "directory_manager.h"
#include "language_manager.h"
#include "log.h"
#include "sync_client.h"
#include "sync_server.h"
#include "tag_serve.h"

// 全局应用桥（QML 侧按类名直接用：ConfigBridge.xxx）。
//
// 用 QML_ELEMENT + QML_SINGLETON 注册进 windows 模块，所以 QML 不需要 import 任何东西；
// 引擎按需惰性创建，全进程只有一个实例，生命周期与引擎一致。
//
// 职责（只做搬运与校验，不自己存第二份真值）：
//   * 持有 core 的 ConfigLoader（./config/config.json）、LanguageManager（./language 目录）、
//     DirectoryConfigManager（./config/path.json）与 TagServe（./config/tag.json + ./config/index.db）。
//   * 启动：ConfigLoader 构造时就读盘（文件不在会落一份默认值）；再用配置里的 DefaultLanguage
//     调 LanguageManager::loadLanguage 加载一次；TagServe 用"有效目录列表 + 配置里的存储模式"构造。
//   * 改设置：QML 写属性 / 调 setLanguage -> 改 ConfigLoader 对应字段 -> saveConfig() 立刻落盘
//     -> 发对应的 changed 信号让 QML 重算（数据源始终以 core 为准，这里不缓存）。
//   * 语言：languageList 是 loadLanguageList("./language") 扫出来的文件名主干；
//     Lang.qml 的 t() 先问本类的 text(id)（core 字典），拿不到才落回 Lang.qml 自带的兜底表。
//   * 标签库 / 索引：条数直读数据库；模式转换、刷新索引、清除失效、导入导出都是 core 的操作，
//     做完发 libraryChanged 让 QML 重算。
//
// 为什么这些 core 对象都放在同一个类里：TagServe 是全进程共享的单实例（它持有数据库和标签库），
// 必须有个唯一的持有者；而且 Log 也在本类里，采集时能一次读完所有 core 类的 error_string_，
// 这正是旧工程那个 Bridge 持有 config_ / dm_ / language_ / ts_ / log_ 的做法。
// 页面级的桥（BrowseBridge / TagsBridge / ...）以后可以再拆，但它们要用到 TagServe 的话，
// 得拿到本类这一份实例，不能各建一个。
//
// 失败统一走 errorOccurred 信号（QML 侧 Store 转成 Toast），也可以用 lastError() 事后取。
//
// 日志：Log 就建在本类里（成员 log_），不再另起一个活得比桥久的日志单例 ——
// 桥和 Log 同生共死，所以既不用登记也不用注销。采集方式与旧工程 bridge.cpp 一致：
// eventFilter 截鼠标/键盘输入（不消费事件），下一轮事件循环再读各类的 error_string_ 写日志。
class ConfigBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ---------------- 只读：真值在 config.json / language 目录里 ----------------
    Q_PROPERTY(QString version READ version NOTIFY configChanged)
    Q_PROPERTY(QString tagMode READ tagMode NOTIFY configChanged)
    Q_PROPERTY(int serverWaitingTime READ serverWaitingTime NOTIFY configChanged)
    Q_PROPERTY(QString downloadPath READ downloadPath NOTIFY configChanged)
    Q_PROPERTY(int broadcastPort READ broadcastPort NOTIFY configChanged)
    Q_PROPERTY(QString broadcastMagicWord READ broadcastMagicWord NOTIFY configChanged)

    // ---------------- 语言（core 的 LanguageManager） ----------------
    Q_PROPERTY(QStringList languageList READ languageList NOTIFY languageListChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)

    // ---------------- 标签库 / 索引（core 的 DirectoryConfigManager + TagServe） ----------------
    Q_PROPERTY(int fileCount READ fileCount NOTIFY libraryChanged)
    Q_PROPERTY(int dirCount READ dirCount NOTIFY libraryChanged)
    // 最近一次标签库 / 索引操作从 core 读回来的数据回报原文（"[tip] <op> key=value ..."），失败时为空串。
    // core 的写操作只回 bool，成功了几个写在 error_string_ 里，上层用 getLastError() 读出来渲染
    Q_PROPERTY(QString lastReport READ lastReport NOTIFY libraryChanged)

    // ---------------- 可写：写即改 ConfigLoader 对应条目并落盘 ----------------
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY themeChanged)

    // ---------------- 同步（core 的 SyncServer / SyncClient） ----------------
    // 服务端是否在跑（SyncServer 的 start/stop 由本类管着）
    Q_PROPERTY(bool serverRunning READ serverRunning NOTIFY syncStateChanged)
    // 待发送目录快照 [{ name, path }]（SyncServer::getTaskQueue() 的搬运，入队/停服/断开后刷新）
    Q_PROPERTY(QVariantList serverQueue READ serverQueue NOTIFY serverQueueChanged)
    // 扫到的局域网设备快照 [{ name, ip, port }]（SyncClient::getServers() 的搬运）
    Q_PROPERTY(QVariantList clientServers READ clientServers NOTIFY clientServersChanged)
    // 客户端是否正在下载（下载中不允许改保存目录）
    Q_PROPERTY(bool clientDownloading READ clientDownloading NOTIFY syncStateChanged)
    // 当前连接的服务端在 clientServers 里的下标（没连接时 -1）。
    // 界面据此把"当前连接的那一台"的下载按钮灰化：别的行只是禁用（点了也没用），不做灰化
    Q_PROPERTY(int connectedServerIndex READ connectedServerIndex NOTIFY syncStateChanged)
    // 最近一次下载落地的任务目录名（客户端 TaskReport::name_，= downloadPath 下的第一层子目录名）
    Q_PROPERTY(QString lastTaskDir READ lastTaskDir NOTIFY syncStateChanged)

public:
    // FontSize 的合法区间（与 ConfigLoader::loadConfig 里的校验一致）
    static constexpr int MIN_FONT_SIZE = 6;
    static constexpr int MAX_FONT_SIZE = 48;

    explicit ConfigBridge(QObject *parent = nullptr);
    ~ConfigBridge() override;

    QString version() const;
    QString tagMode() const;
    int serverWaitingTime() const;
    QString downloadPath() const;
    int broadcastPort() const;
    QString broadcastMagicWord() const;

    QStringList languageList() const;
    QString language() const;

    int fontSize() const;
    void setFontSize(int size);

    int theme() const;
    void setTheme(int theme);

    // 切语言：入参必须是 languageList 里的文件主干名（例如 "zh-cn" / "en-us"）。
    // 成功 -> 改配置的 DefaultLanguage + 落盘 + 发 languageChanged；语言确实换成了就返回 true
    // （落盘失败只报 errorOccurred，不回滚内存里的语言）。
    Q_INVOKABLE bool setLanguage(const QString &language_name);

    // 重新扫 ./language（用户往目录里丢了新语言文件后调一次）
    Q_INVOKABLE bool reloadLanguageList();

    // 当前语言下 id 对应的文本；core 字典里没有这个 id 时返回空串（QML 那边据此落回兜底表）
    Q_INVOKABLE QString text(const QString &id) const;

    // 手动落盘（正常改设置会自动落，这个留给以后需要显式落盘的场景）
    Q_INVOKABLE bool save();

    // ---------------- 标签库 / 索引 ----------------

    // files 表的记录条数（索引里有多少条记录，含目录记录）；失败返回 -1
    int fileCount() const;
    // 受管目录条数（含失效的，与配置里的列表一致）
    int dirCount() const;

    // 最近一次操作的数据回报原文（空串 = 上次操作没有回报或失败了）
    QString lastReport() const;

    // 从最近一次回报里取某个键的数字（例："removed" / "deduped" / "indexed" / "converted" /
    // "roots" / "types" / "tags"）；键不存在或不是数字时返回 -1
    Q_INVOKABLE int reportValue(const QString &key) const;

    // 存储模式整体转换：Sidecar <-> Filename。
    // core 的 convertMode(from, to, false) 会跑两遍（先写新格式、成功后再删旧格式）并重建索引；
    // 成功后把新模式写回 config.json 的 TagMode 并落盘，然后发 libraryChanged。
    Q_INVOKABLE bool convertMode();

    // 刷新索引：用受管目录列表整体重扫（TagServe::reLoadRoot(全部根)，内部先 clearAll 再按磁盘重扫）
    Q_INVOKABLE bool refreshIndex();

    // 清除失效 / 重复数据：只清理数据库记录（磁盘上已不存在的记录 + 重复记录），不动真实文件
    Q_INVOKABLE bool clearInvalidData();

    // 导出标签库：先把 core 的 tag.json 存一次，再把它复制到目标位置。
    // 入参可以用 QML FileDialog 给的 file:// URL，也可以直接给本地路径。
    Q_INVOKABLE bool exportLibrary(const QString &target_url);

    // 导入标签库：合并另一个 tag.json（类型 / 标签全局唯一，只补充本库没有的）
    Q_INVOKABLE bool importLibrary(const QString &source_url);

    // ---------------- 同步：服务端 ----------------

    bool serverRunning() const;
    QVariantList serverQueue() const;

    // 开 / 关服务端。server_name 留空时用默认名 "tagmeow"。
    // 启动用配置里的广播端口，TCP 端口传 0 由系统分配（core 会把实际端口广播出去）
    Q_INVOKABLE bool toggleServer(const QString &server_name);

    // 把某个目录推进发送队列（服务端没启动 / 目录无效时返回 false 并报错）
    Q_INVOKABLE bool enqueueServerDir(const QString &dir_path);

    // 断开当前客户端连接并清空队列（随后重新开始广播）
    Q_INVOKABLE bool disconnectServerClient();

    // ---------------- 同步：客户端 ----------------

    bool clientDownloading() const;
    // 当前连接的服务端下标（没在连接时 -1）
    int connectedServerIndex() const;
    QVariantList clientServers() const;
    QString lastTaskDir() const;

    // 扫描局域网（core 是阻塞扫描，最多两秒左右）
    Q_INVOKABLE bool scanServers();

    // 下载指定下标的设备（下标来自 clientServers）
    Q_INVOKABLE bool startDownload(int server_index);

    // 清除下载记录缓存（不删已下载的文件）
    Q_INVOKABLE void clearDownloadRecords();

    // 断开当前下载会话
    Q_INVOKABLE void disconnectClient();

    // 改保存目录：入参是 QML FolderDialog 给的 file:// URL（或本地路径）。
    // 正在下载时拒绝（core 的 SyncClient 构造时固定下载路径，只能整体重建）
    Q_INVOKABLE bool setDownloadPath(const QString &url);

    // 把"具体下载目录"（downloadPath/<最近一次下载的任务目录>）加入受管目录并建索引。
    // 还没下载过任务目录时，退回 downloadPath 本身
    Q_INVOKABLE bool addDownloadDirToLibrary();

    // 最近一次失败说明（成功调用后会清空），失败文案同时通过 errorOccurred 发出
    Q_INVOKABLE QString lastError() const;

signals:
    void configChanged();
    void languageListChanged();
    void languageChanged();
    void fontSizeChanged();
    void themeChanged();
    // 标签库 / 索引变动（条数、模式、导入导出之后）：QML 据此重算显示
    void libraryChanged();
    // 同步状态变动（服务端启停 / 下载开始结束 / 任务目录更新）
    void syncStateChanged();
    // 待发送队列快照变动：QML 据此重建队列列表
    void serverQueueChanged();
    // 扫描结果变动：QML 据此重建设备列表
    void clientServersChanged();
    // 同步过程提示：只传「文案键 + 一个参数」，由 QML 侧用 Lang.t(key, arg) 翻译后显示
    //（这样文案仍然只有 Lang.qml 一份，桥不掺和措辞）
    void syncMessage(const QString &message_id, const QString &arg);
    // 内部用：工作线程每完成一个入队目录发一次（携带任务目录名）。
    // 跨线程 auto 连接会排队回主线程，本类在那里把它记成"具体下载目录"
    void clientTaskReported(const QString &task_dir);
    void errorOccurred(const QString &message);

protected:
    // 输入流截获：鼠标抬起 / 按键抬起 -> 下一轮事件循环收一轮日志（不消费事件）
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 已加载的语言名；一个字都没加载进来时退回配置里的 DefaultLanguage（界面上仍有东西可显示）
    QString effectiveLanguageName() const;
    // 落盘 + 广播 configChanged；失败发 errorOccurred
    bool persist();
    void reportError(const QString &message);
    // 成功后的 [tip] 只记日志、不弹 Toast（旧工程里 error_string_ 同时承载错误与提示）
    void setTip(const QString &message);

    // 逐个读各类的 error_string_ / getLastError()：空串跳过、与上次相同跳过、变了才落一条
    void collectLogMessages();
    void logOne(const std::string &class_name, const std::string &message);
    // 立刻落一条（不等下一次鼠标/键盘交互）：同步是异步流程，交互后收集会把最后一次操作留在内存里
    void logNow(const std::string &message);

    // file:// URL 或本地路径 -> core 用的 UTF-8 本地路径
    static std::filesystem::path localPathFromUrl(const QString &url);
    // 记下这一次操作从 core 的 out 参数拿到的数据回报（同时拼好人看的 lastReport 文本）
    void setReport(const QVariantMap &values);

    // ---- 同步：内部 ----
    // 惰性创建：SyncClient 构造时会绑 UDP 广播端口，没进过同步页就不该占着它
    void ensureServer();
    void ensureClient();
    // 把 SyncServer::getTaskQueue() / SyncClient::getServers() 搬成快照并广播
    void refreshServerQueue();
    void refreshClientServers();

    // 声明顺序 = 构造顺序：ts_ 要用到 config_ 的存储模式与 dm_ 的有效目录列表，必须排在它们后面
    ConfigLoader config_;
    DirectoryConfigManager dm_;
    LanguageManager language_;
    TagServe ts_;

    QString last_error_;
    // 上一次操作从 core 的 out 参数读回来的数据回报（键值对；reportValue() 查这里）
    QVariantMap last_values_;
    // 同一份数据拼成的文本（"indexed=3 roots=1"），给人看 / 进日志
    QString last_report_;

    // 日志（直接复用旧工程的 Log 类）：默认 ./config/log.txt，超 1MB 自动清空
    Log log_;
    // 各类上次已写入日志的消息（相同消息不重复写）
    std::unordered_map<std::string, std::string> last_logged_;

    // 同步：服务端 / 客户端（与旧工程一样惰性创建，析构里先 stop 再销毁）
    std::unique_ptr<SyncServer> s_server_;
    std::unique_ptr<SyncClient> s_client_;
    // SyncServer 没有"在跑吗"的公开查询，自己记着（stop 成功 / start 成功时更新）
    bool server_running_flag_ = false;
    QVariantList server_queue_;
    QVariantList client_servers_;
    QString last_task_dir_;
    // 最近一次发起下载时选中的服务端下标；只在主线程写（startDownload），
    // 对外由 connectedServerIndex() 在"正在连接/下载"期间才暴露出去
    int connected_server_index_ = -1;
};

#endif // CONFIG_BRIDGE_H
