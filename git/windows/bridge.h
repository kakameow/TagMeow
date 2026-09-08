#ifndef BRIDGE_H
#define BRIDGE_H

#include <QObject>
#include <QVariantList>
#include <memory>

#include "directory_manager.h"
#include "language_manager.h"
#include "tag_serve.h"
#include "sync_server.h"
#include "sync_client.h"
#include "transferdata.h"
#include "config_loader.h"

// 前端桥接：接收 QML 信号 -> 调用 TransferData 处理 -> 按返回值刷新 Main.qml 控件
class Bridge : public QObject
{
    Q_OBJECT
public:
    explicit Bridge(QObject *parent = nullptr);
    ~Bridge();

    // 引擎加载 Main.qml 完成后调用：定位控件 + 绑定 QML 信号 + 初始数据填充
    void bindTo(QObject *root);

public slots:
    // searchButton: 三容器(包含/排除/只有)标签 -> 搜索 -> 成功刷新 FileContainer
    void onSearchClicked();
    // FileContainer 某条记录标签 添加/删除/修改
    void onFileTagAdded(const QString &path, const QString &tag);
    void onFileTagRemoved(const QString &path, const QString &tag);
    void onFileTagChanged(const QString &path, const QString &oldTag, const QString &newTag);
    // optionsBar: 添加目录/标签/类型 与 删除目录/类型/标签
    void onAddDirClicked();
    void onAddTagClicked();
    void onAddTypeClicked();
    void onRemoveDirClicked();
    void onRemoveTypeClicked();
    void onRemoveTagClicked();
    // resetTypeColor 按钮: typeInput(类型名) + colorInput(颜色) -> TagServe::setTypeColor
    void onResetTypeColorClicked();
    // DirContainer: 双击某目录 -> getDirFile 取该目录文件 -> 成功刷新 FileContainer
    void onDirDoubleClicked(const QString &path);
    // LibraryTag: 点击类型名行(展开/收起) -> 回填 typeInput
    void onLibraryTypeClicked(const QString &type);
    // FileContainer 双击某行: 目录 -> 打开该目录; 文件 -> 打开所在目录并高亮选中(explorer /select)
    void onFileDoubleClicked(const QString &path);
    // refreshButton: 强制刷新(重新校验目录 / 重载标签 / 重载数据库根) 后更新渲染
    void onRefreshClicked();
    // clearButton: 清空 包含/排除/只有 三个容器的标签列表
    void onClearClicked();
    // setWindow 确认按钮: 读取设置窗口控件 -> 保存 config.json -> 退出程序(重启生效)
    void onSaveConfigClicked();
    // optionsDialog 确认(Yes): 镜像 CLI convertmode —— 翻转默认存储模式并转换, 成功后更新 config_ 并保存
    void onConvertModeConfirmed();
    // syncWindow 服务器(分享): start/stop/add目录/disconnect
    void onServerStartClicked();
    void onServerStopClicked();
    void onServerAddDirClicked();
    void onServerDisconnectClicked();
    // syncWindow 客户端(下载): scan/download/clear/disconnect
    void onClientScanClicked();
    void onClientDownloadClicked();
    void onClientClearClicked();
    void onClientDisconnectClicked();
    // 首次点击 snycWindow(打开同步窗口)时惰性创建 SyncServer/SyncClient 实例(仅创建一次)
    void onSyncWindowOpened();
    // 设置窗口: 字号/主题 修改即生效(更新 config_ 并保存 立即刷新渲染)
    void onFontSizeChanged(); // SpinBox.valueChanged 无参信号 -> 槽内读取控件值
    void onThemeChanged(int index); // ComboBox.activated(int) 带参信号

signals:
    // SyncServer/SyncClient 工作线程回调 -> 主线程刷新状态栏(跨线程自动排队)
    void syncServerTip(const QString &msg);
    void syncClientTip(const QString &msg);

private slots:
    void onServerTipArrived(const QString &msg);
    void onClientTipArrived(const QString &msg);

private:
    void pushFileList(); // path_tags_ -> FileContainer.fileList
    void pushDirList();  // path_list_  -> DirContainer.dirList
    void pushTagList();  // type_tags_/type_color_ -> LibraryTag.typeList
    void pushConfig();   // config_ -> setWindow 控件(Version/Port/Magic/TagMode 只读 + 可编辑项)
    void pushServerQueue(); // s_server_->getTaskQueue() -> syncWindow.serverQueue
    void pushServerList();  // s_client_->getServers()  -> syncWindow.serverList
    void setStatusLabel(const char *name, const QString &text); // 写入 gray 状态标签
    void pushUiText();      // language_ -> Main.qml window.uiText 字典(供 QML 文案绑定)
    void pushTheme();       // config_.theme_ -> window.uiColor/themeNames + 应用调色板
    void pushLanguageList(); // loadLanguageList 结果 -> window.languageNames + 选中当前语言
    // 取当前语言文本: id 查 language_ 字典 缺失/语言文件未加载时回退 zh 文案
    QString uiText(const char *id, const char *zh) const;
    void doSearch();     // 读取三容器标签 -> 搜索 -> 刷新 FileContainer
    QObject *findObject(const char *name) const;
    QString textOf(const char *name) const;

    // 后端（生命周期由本类持有 与程序一致）
    ConfigLoader config_;
    DirectoryConfigManager dm_;
    LanguageManager language_;
    TagServe ts_;
    // SyncServer/SyncClient 惰性实例: 不在构造函数初始化 首次点击 snycWindow 时创建(见 onSyncWindowOpened)
    std::unique_ptr<SyncServer> s_server_;
    std::unique_ptr<SyncClient> s_client_;
    TransferData td_;

    QObject *root_ = nullptr;
    QObject *fileContainer_ = nullptr;
    QObject *dirContainer_ = nullptr;
    QObject *libraryTag_ = nullptr;
    QObject *includeContainer_ = nullptr;
    QObject *excludeContainer_ = nullptr;
    QObject *onlyContainer_ = nullptr;
};

#endif // BRIDGE_H
