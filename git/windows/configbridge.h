#ifndef CONFIG_BRIDGE_H
#define CONFIG_BRIDGE_H

#include <QHash>
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
    // 受管目录快照 [{ name, path, count, valid }]（core 的 DirectoryConfigManager + 索引里该目录下的文件数）。
    // path 用系统分隔符（只用于显示，增删改一律按下标走），name 取路径最后一段。
    // 真值仍在 core：这里是只读快照，任何会动目录/文件数的操作都发 dirsChanged 让页面重取
    Q_PROPERTY(QVariantList dirs READ dirs NOTIFY dirsChanged)
    // 浏览页的搜索结果（平铺文件列表，每次 search() 之后重建）：
    // [{ fileName, dirPath, sizeText, timeText, iconKind, tags: [{ name, tone }], path, kind: "file" }]。
    // core 的索引里连目录也有记录，但浏览页这一版是平铺文件列表，目录记录不进结果
    Q_PROPERTY(QVariantList browseFiles READ browseFiles NOTIFY browseChanged)
    // 是否正在查询：按了搜索按钮到结果喂完之间为 true。
    // 界面据此在文件列表里渲染加载态（筛选容器的增删一律不触发查询，只有搜索按钮触发）
    Q_PROPERTY(bool searching READ searching NOTIFY searchStateChanged)
    // 层级浏览（不平铺）：当前这一层的直接子项，第一条固定是 "."（返回上一层，kind = "parent"）。
    // 行字段与平铺结果**完全一样**（fileName / dirPath / sizeText / timeText / iconKind / tags / path），
    // 所以列表用同一套行渲染，只是 kind 多出 "parent" 与 "dir" 两种
    Q_PROPERTY(QVariantList browseEntries READ browseEntries NOTIFY browseEntriesChanged)
    // 当前层级路径（空串 = 不在层级模式，界面显示平铺搜索结果）
    Q_PROPERTY(QString browseLevel READ browseLevel NOTIFY browseEntriesChanged)

    // ---------------- 标签库：类型 / 标签（core 的 TagServe 标签库部分） ----------------
    // core 的标签库按**名字**索引，界面按**数字 id** 索引（页面里到处是 typeId/tagId 与 > 0 的判断），
    // 所以本类维护一份稳定的 名字<->id 映射，对外只暴露数字 id。
    // types: [{ typeId, name, color }]；tags: [{ tagId, typeId, name }]（标签全局唯一，tagId 也是全局的）
    Q_PROPERTY(QVariantList types READ types NOTIFY typesChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)

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

    // 受管目录快照（读的时候如果被置脏了就顺手重建一次，见 rebuildDirs）
    QVariantList dirs();

    // ---------------- 受管目录（目录页面） ----------------
    // 页面按下标操作：快照顺序与 core 的 getDirectories() 一致，下标在两次读取之间不会乱序

    // 加受管目录：入参是 QML FolderDialog 给的 file:// URL（也接受本地路径）。
    // 路径无效 / 已经在列表里 -> 报错并返回 false（core 的 addDirectory 自己判断并把原因写在 error_string_）
    Q_INVOKABLE bool addDir(const QString &dir_url);

    // 移除第 index 个受管目录：先摘白名单再删索引记录，索引删失败会把白名单加回去（不留半状态）。
    // 磁盘上的文件一个都不碰
    Q_INVOKABLE bool removeDir(int index);

    // 单目录刷新：TagServe::reLoadRoot(该目录) —— 只重扫这一个目录（含子目录），
    // 不动其它根的记录，也不改根列表
    Q_INVOKABLE bool refreshDir(int index);

    // 重读 ./config/path.json 并让 core 按磁盘重算每个目录的有效性（onCompleted 时核一次）。
    // core 没有"单独重算有效性"的接口，loadFromFile 会重算 is_valid_ 且不删任何条目
    Q_INVOKABLE bool revalidateDirs();

    // 打开第 index 个受管目录（切到浏览页并进到该目录）。
    // 浏览页还没接 core，现在只是记日志 + 给一条提示，接口先留着，接上后只改这里
    Q_INVOKABLE bool openDir(int index);

    // ---------------- 浏览页：查询与文件赋值 ----------------

    // 结果快照（读的时候就是上一次 search() 的结果）
    QVariantList browseFiles() const;
    // 是否正在查 / 正在把结果分批喂进 browseFiles
    bool searching() const;

    // 层级浏览：当前层条目 + 当前层路径
    QVariantList browseEntries() const;
    QString browseLevel() const;

    // 进入某个目录（层级渲染这一层）：读 core 索引里这一层的直接子项
    Q_INVOKABLE bool enterDir(const QString &dir_path);
    // 返回上一层（当前层最上面那条 "."）
    Q_INVOKABLE bool goUpLevel();
    // 退出层级模式（按搜索按钮时用：切回平铺搜索结果）
    Q_INVOKABLE void exitLevel();
    // 用系统方式打开（文件 = 关联程序 / 目录 = 资源管理器）
    Q_INVOKABLE bool openEntryInSystem(const QString &path);
    // 在系统文件管理器里定位（目录 = 打开该目录；文件 = 打开所在目录并选中该文件）
    Q_INVOKABLE bool revealInSystem(const QString &path);

    // 按三个标签容器 + 目录筛选查一次，结果搬进 browseFiles 并发 browseChanged。
    // include / exclude / only 传标签名；dirs 传目录路径（空列表 = 不限目录，core 的语义）。
    // 目录一个都没勾时界面会传一个不存在的路径（哨兵）-> 干净的零结果，不用 core 特判
    Q_INVOKABLE bool search(const QStringList &include, const QStringList &exclude,
                            const QStringList &only, const QStringList &dirs);

    // 给文件加标签 / 去掉标签（标签小片拖到文件行上的落点）。
    // Filename 模式下 core 会改文件名，所以写完按上一次的查询条件重查一遍，界面拿到的路径也是新的
    Q_INVOKABLE bool assignTagToFile(const QString &file_path, const QString &tag);
    Q_INVOKABLE bool removeTagFromFile(const QString &file_path, const QString &tag);

    // 从索引里移除一条文件记录（只删数据库记录，磁盘上的文件不动）
    Q_INVOKABLE bool removeFileFromIndex(const QString &file_path);

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

    // ---------------- 标签库：类型 / 标签（页面按 id 调，桥翻成 core 的名字） ----------------

    QVariantList types() const;
    QVariantList tags() const;

    // 新建类型。**同名类型已存在时改成"重设颜色"**：
    // core 的 TagLibrary::addType 遇到同名是静默 no-op（连颜色都不改），
    // 所以这里先判断，已存在就直接走 setTypeColor，并提示"颜色已更新"。
    // color 非法时 core 会把它改写成 #FFC0CB（引用回传），返回值里能看出来。
    Q_INVOKABLE bool addType(const QString &type_name, const QString &color);

    Q_INVOKABLE bool removeType(int type_id);
    // 重命名类型：**按名字**（旧名 -> 新名），不是按 id ——
    // 页面的语义是"把类型名称输入框里的那个类型改成重命名输入框里的名字"，源类型由输入框内容决定。
    // core 会把该类型下的标签和颜色一起搬过去；新名字已存在、或旧名字根本不存在时返回 false
    Q_INVOKABLE bool renameType(const QString &old_name, const QString &new_name);
    Q_INVOKABLE bool setTypeColor(int type_id, const QString &color);

    // 标签全局唯一：core 里已存在同名标签时是静默 no-op（返回 true 但不做任何事）
    Q_INVOKABLE bool addTag(int type_id, const QString &tag_name);
    Q_INVOKABLE bool removeTag(int tag_id);

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
    // 受管目录快照变动（增删目录 / 刷新 / 有效性重算 / 文件数变化）：QML 据此重取 dirs
    void dirsChanged();
    // 搜索结果变动（每次 search() / 文件标签写完重查之后）：QML 据此重建平铺列表
    void browseChanged();
    // 查询开始 / 结束（连加载态用）
    void searchStateChanged();
    // 层级条目变动（进目录 / 返回上层 / 标签写完重扫）
    void browseEntriesChanged();
    // 标签库的类型 / 标签列表变动（增删改之后）
    void typesChanged();
    void tagsChanged();
    // 标签库操作的提示：同样只传「文案键 + 一个参数」，QML 侧用 Lang.t(key, arg) 翻译
    void libraryMessage(const QString &message_id, const QString &arg);
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

    // ---- 标签库：内部 ----
    // 读 core 的标签库 -> 重建 types/tags 快照 -> 发信号；顺带分配 / 回收 名字<->id 映射
    void refreshTagLibrary();
    // id -> core 用的名字（找不到返回空串）
    QString typeNameOfId(int type_id) const;
    QString tagNameOfId(int tag_id) const;

    // ---- 受管目录：内部 ----
    // 快照只被"置脏 + 发信号"，真正重建发生在页面来读 dirs() 的时候：
    // 重建要全表查一次再逐条 stat 判目录（Directory 记录也在 files 表里），
    // 挂在每次 libraryChanged 上做太亏，页面没打开时更是白做
    void invalidateDirs();
    void rebuildDirs();
    // 第 index 个受管目录（越界返回 nullptr）
    const Directory *dirAt(int index) const;

    // ---- 浏览页：内部 ----
    // core 的记录 -> 平铺文件行（目录记录挑掉；文件名 / 大小 / 时间 / 图标种类 / 标签小片都在这儿拼好）
    QVariantList buildBrowseRows(const std::vector<table::FileInfo> &rows) const;
    // 单条记录 -> 一行（分批喂的时候一条一条拼）。keep_dirs = true 时目录记录也成行（层级模式要目录行）
    QVariantMap makeBrowseRow(const table::FileInfo &info, bool keep_dirs = false) const;
    // ---- 层级浏览：内部 ----
    // 进到某个目录（记下当前层与上一层，读这一层的直接子项）
    bool enterLevel(const std::filesystem::path &dir);
    // 读某一层的直接子项（第一条是 "."，条件不满足时不给）
    QVariantList buildLevelEntries(const std::filesystem::path &dir) const;
    // 标签写完 / 索引变化后按当前层级重扫（不在层级模式就什么都不做）
    void refreshLevel();
    // 真正去 core 查一次（search() 把它排到下一轮事件循环，好让界面先把加载态画出来）
    bool runPendingSearch();
    // 把 pending_infos_ 里剩下的一批结果拼进 browse_files_ 并广播（一次一小批 = 列表动态加载渲染）
    void feedBrowseBatch();
    // 按上一次 search() 的条件重查一次（标签写完刷新列表用；还没查过就什么都不做）
    bool rerunLastSearch();

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

    // 标签库快照 + 名字<->id 映射（id 一旦分配就跟着名字走，刷新时不会因为顺序变化而错位）
    QVariantList types_;
    QVariantList tags_;
    QHash<QString, int> type_ids_;
    QHash<QString, int> tag_ids_;
    int next_type_id_ = 1;
    int next_tag_id_ = 1;

    // 受管目录快照（懒重建：脏了才在 dirs() 里重算）
    QVariantList dirs_;
    bool dirs_dirty_ = true;

    // 浏览页：搜索结果快照 + 上一次的查询条件（标签写完之后按同一条件重查）
    QVariantList browse_files_;
    QStringList last_include_;
    QStringList last_exclude_;
    QStringList last_only_;
    QStringList last_dirs_;
    bool has_searched_ = false;
    // 正在查 / 正在分批喂结果
    bool searching_ = false;
    // 已经排了一次查询但还没跑（连点搜索按钮只会跑最后一次）
    bool search_pending_ = false;
    // core 查回来的全部记录 + 已经喂到哪一条（分批喂给界面）
    std::vector<table::FileInfo> pending_infos_;
    int pending_offset_ = 0;

    // 层级浏览：当前层条目 / 当前层路径 / 上一层路径（空 = 已经在最上层，不画 "."）
    QVariantList browse_entries_;
    std::filesystem::path level_path_;
    std::filesystem::path level_parent_;
};

#endif // CONFIG_BRIDGE_H
