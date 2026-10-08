pragma Singleton
import QtQuick
// control/ 下的单例（Lang）：子目录不是隐式可见的，必须显式 import。
// 同步过程的提示由桥发「文案键 + 参数」，在这里用 Lang.t 翻译成文本再弹 Toast
import "control"

// 纯 UI 态单例：只放"不需要 core 参与"的界面交互状态与展示信号。
//
// 为什么有这么个文件（与迁移原则对应）：
//   QML 只负责「渲染」+「把用户交互用信号发出去」，桥接层（C++）接 core 处理完再通知 QML 重绘。
//   但有一类交互跟 core 毫无关系 —— 切到哪一页、列表里选中了谁、筛选面板加了哪几条条件、
//   字号/主题长什么样、要不要弹一条 Toast —— 这些纯粹是"界面当下的样子"，
//   送进桥接层再绕回来只会白白多一圈往返。它们就留在这里。
//
// 本文件不含任何数据，也不碰 core：
//   * 文件 / 类型 / 标签 / 目录（files / types / tags / dirs）一律由各页面的桥接层下行，
//     这里不提供，也不缓存。
//   * 任何需要 core 参与的动作都不在这里：查询与筛选生效（runQuery）、层级进出（enterDir /
//     goUpLevel / exitBrowse）、目录勾选（dirChoices / toggleDirChoice，要 core 的目录表才能派生）、
//     标签与类型增删改、导入导出、库刷新、同步启停…… 全部走页面的信号上行。
//   * 本文件里没有任何写死的业务数据：没有文件名、路径、计数、类型名、标签名、服务端名。
//
// 数据源以 core cpp 为准：下面少数几个默认值（fontSize / theme / language）只是
// "桥还没接上时界面能跑"的兜底，桥接层接上后由 core 的真值下行覆盖。
// 文案统一走 control/Lang.qml 单例，本文件没有中文字面量。

QtObject {
    id: store

    // =========================================================================
    // 纯展示信号（只让界面动起来，不产生数据）
    // =========================================================================

    // 提示浮条：Main.qml 的 Connections 接 onToastRequested 弹 Toast
    signal toastRequested(string message)

    // 「标签已赋值」弹窗：由页面的桥在赋值成功后 emit（Main.qml 已接 onTagAssigned）
    signal tagAssigned(string tag, string fileName, string color)

    // 页面自己弹一条提示（例：点了个还没接 core 的按钮，先给个反馈）
    function toast(message) {
        toastRequested(message)
    }

    // =========================================================================
    // 页面切换
    // =========================================================================

    // 取值：browse / dirs / tags / sync / settings
    // Main.qml 的 StackLayout 按它切 currentIndex，侧栏 NavItem 也按它高亮
    property string currentPage: "browse"

    function goTo(page) {
        currentPage = page
    }

    // =========================================================================
    // 浏览页：层级浏览的导航态（只有状态，取数据是桥的事）
    // =========================================================================

    // browsePath 非空 = 正在按目录层级浏览；为空 = 平铺的搜索结果模式
    // browseParent 为空 = 已经在最上层（界面上不画"返回上层"那一行）
    property string browsePath: ""
    property string browseParent: ""

    function setBrowseLevel(path, parent) {
        browsePath = path
        browseParent = (parent === undefined || parent === null) ? "" : parent
    }

    function clearBrowseLevel() {
        browsePath = ""
        browseParent = ""
    }

    // =========================================================================
    // 浏览页：筛选面板的纯 UI 态
    // =========================================================================

    // 筛选条件：[{ kind, tag, color }]，kind = include / exclude / only，初始为空（用户点选才加）。
    // 增删只是改这份列表；真正重新查询由筛选面板去发信号，Store 不碰 core。
    property var filters: []

    // color 可选：给得出就带着（筛选条上的小圆点），给不出留空
    function addFilter(kind, tag, color) {
        var next = filters.slice()
        next.push({
            kind: kind,
            tag: tag,
            color: (color === undefined || color === null) ? "" : color
        })
        filters = next
    }

    function removeFilter(index) {
        if (index < 0 || index >= filters.length)
            return
        var next = filters.slice()
        next.splice(index, 1)
        filters = next
    }

    function clearFilters() {
        filters = []
    }

    // =========================================================================
    // 列表选中项（标签库页用）
    // =========================================================================

    property int selectedTypeId: -1
    property int selectedTagId: -1

    // 新建类型时的默认颜色：这是界面默认值，不是业务数据
    property string typeColor: "#ffb3c6"

    // =========================================================================
    // 同步页：输入 / 选择态
    // =========================================================================

    // 「服务器名称」输入框与它双向绑定（敲什么写什么）。
    // 默认名 tagmeow：留空时桥也会兜底成这个名字
    property string serverName: "tagmeow"

    // 客户端面板选中的设备行（-1 = 一台都没选）；设备列表本身由桥下行
    property int selectedServerIndex: -1

    // 选中某台设备（纯 UI 态）：点行 / 点该行的下载按钮都走这里。
    // 注意：这个函数必须存在 —— 页面里是 "Store.selectServer(i); ConfigBridge.startDownload(i)"
    // 两行连着写，少了它第一行就抛 TypeError，第二行永远执行不到（界面上表现为点了没反应）
    function selectServer(index) {
        selectedServerIndex = index
    }

    // =========================================================================
    // 桥接层的提示 -> Toast
    // =========================================================================

    // 字号 / 主题 / 语言的真值都在 core（./config/config.json），由 ConfigBridge 暴露给 QML，
    // 本文件不存第二份：Theme.baseFontSize 读 ConfigBridge.fontSize、Lang.current 读
    // ConfigBridge.language、设置页直接写 ConfigBridge.theme / 调 ConfigBridge.setLanguage。
    // 桥里失败时统一发 errorOccurred，这里转成 toastRequested，由 Main.qml 弹 Toast。
    // 注意：QtObject 没有 default property，子对象不能裸写，必须挂在一个属性上
    readonly property Connections bridgeErrorRelay: Connections {
        target: ConfigBridge

        function onErrorOccurred(message) {
            store.toast(message)
        }

        // 同步过程提示：桥只给文案键 + 一个参数，措辞留在 Lang.qml 一份（Lang.t 取不到键会原样返回键名）
        function onSyncMessage(messageId, arg) {
            store.toast(Lang.t(messageId, arg))
        }
    }
}
