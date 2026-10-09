pragma Singleton

import QtQuick
import "control"

// 纯 UI 态单例：只放"不需要 core 参与"的界面交互状态与展示信号

QtObject {
    id: store

    // 纯展示信号（只让界面动起来 不产生数据）
    // 提示浮条：Main.qml 的 Connections 接 onToastRequested 弹 Toast
    signal toastRequested(string message)

    // 「标签已赋值」弹窗：由页面的桥在赋值成功后 emit（Main.qml 已接 onTagAssigned）
    signal tagAssigned(string tag, string fileName, string color)

    // 页面自己弹一条提示
    function toast(message) {
        toastRequested(message)
    }

    // 页面切换
    // 取值：browse / dirs / tags / sync / settings
    // Main.qml 的 StackLayout 按它切 currentIndex 侧栏 NavItem 也按它高亮
    property string currentPage: "browse"

    function goTo(page) {
        currentPage = page
    }

    // 浏览页：层级浏览的导航态（只有状态 取数据是桥的事）
    // browsePath 非空 = 正在按目录层级浏览 为空 = 平铺的搜索结果模式
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

    // 浏览页：筛选面板的纯 UI 态
    // 筛选条件：[{ kind, tag, color }]kind = include / exclude / only 初始为空
    // 增删只是改这份列表（渲染出 chip）真正交给 core 查询是「搜索」按钮的事（runQuery）
    property var filters: []

    // color 可选：给得出就带着（筛选条上的小圆点）给不出就从标签库（core）里查一次类型色
    function addFilter(kind, tag) {
        for (var i = 0; i < filters.length; i++) {
            if (filters[i].kind === kind && filters[i].tag === tag) {
                toast(Lang.t("filter.already_added"))
                return
            }
        }
        var next = filters.slice()
        next.push({ kind: kind, tag: tag, color: tagColorOf(tag) })
        filters = next
        toast(Lang.t("filter.added_prefix") + tag)
    }

    function removeFilter(index) {
        if (index < 0 || index >= filters.length)
            return
        var next = filters.slice()
        next.splice(index, 1)
        filters = next
        toast(Lang.t("filter.removed"))
    }

    // 清空：只清标签筛选
    function clearFilters() {
        if (filters.length === 0)
        {
            return
        }
        filters = []
        toast(Lang.t("filter.cleared"))
    }

    // 标签名 -> 类型色（标签库里查一次 查不到就不带小圆点）
    function tagColorOf(tag) {
        var tags = ConfigBridge.tags
        var types = ConfigBridge.types
        for (var i = 0; i < tags.length; i++) {
            if (tags[i].name !== tag)
                continue
            for (var j = 0; j < types.length; j++) {
                if (types[j].typeId === tags[i].typeId)
                    return types[j].color
            }
        }
        return ""
    }

    // 当前筛选条件的摘要
    function filterSummary() {
        var include = 0
        var exclude = 0
        var only = 0
        for (var i = 0; i < filters.length; i++) {
            if (filters[i].kind === "include")
                include++
            else if (filters[i].kind === "exclude")
                exclude++
            else
                only++
        }
        if (include + exclude + only === 0)
            return Lang.t("filter.summary_none")
        var parts = []
        if (include > 0)
            parts.push(Lang.t("cnt.include") + " " + include)
        if (exclude > 0)
            parts.push(Lang.t("cnt.exclude") + " " + exclude)
        if (only > 0)
            parts.push(Lang.t("cnt.only") + " " + only)
        return parts.join(" · ")
    }

    // 浏览页：目录多选（勾选态是纯 UI 态 目录表本身来自 core 的 ConfigBridge.dirs）
    // 稀疏表 path -> bool：只记"用户改过的"目录 没记录的按默认 true（全选）算
    // 所以 core 增删目录不会把用户已经点过的选择重置掉
    property var dirChecked: ({})

    // "一个目录都不选"：core 里 dirs 为空表示"不限制目录" 所以要用一个不可能存在的路径当哨兵
    readonly property string noDirSentinel: "\u0001__tagmeow_no_dir__"

    function isDirChecked(path) {
        return dirChecked.hasOwnProperty(path) ? dirChecked[path] : true
    }

    // 目录多选列表（浏览页筛选面板用）：{ path, label, checked } 全部从 core 的目录表派生
    // 目录增删后这里自动跟着变 不写死任何路径
    readonly property var dirChoices: {
        var out = []
        var dirs = ConfigBridge.dirs
        for (var i = 0; i < dirs.length; i++)
            out.push({ path: dirs[i].path, label: dirs[i].name, checked: isDirChecked(dirs[i].path) })
        return out
    }

    function toggleDirChoice(index) {
        var choices = dirChoices
        if (index < 0 || index >= choices.length)
            return
        var item = choices[index]
        var next = {}
        for (var path in dirChecked)
            next[path] = dirChecked[path]
        next[item.path] = !item.checked
        dirChecked = next
    }

    // 目录筛选触发器显示 完整路径（勾了多个就用 " · " 串起来） 空则提示未选择
    function dirPathsSummary() {
        var dirs = ConfigBridge.dirs
        var picked = []
        for (var i = 0; i < dirs.length; i++) {
            if (isDirChecked(dirs[i].path))
                picked.push(dirs[i].path)
        }
        if (picked.length === 0)
            return Lang.t("dir.none_selected")
        return picked.join(" · ")
    }

    function dirSummary() {
        var n = 0
        var dirs = ConfigBridge.dirs
        for (var i = 0; i < dirs.length; i++) {
            if (isDirChecked(dirs[i].path))
                n++
        }
        return n > 0 ? Lang.t("dir.selected_count", n) : Lang.t("dir.none_selected")
    }

    // 按「搜索」按钮：把三个标签容器的值 + 目录勾选的值一起交给桥 桥再交 core 处理
    // 结果不由这里存 core 处理完桥发 browseChanged 界面重取 ConfigBridge.browseFiles
    function runQuery() {
        var include = []
        var exclude = []
        var only = []

        for (var i = 0; i < filters.length; i++) {
            var item = filters[i]
            if (item.kind === "include")
                include.push(item.tag)
            else if (item.kind === "exclude")
                exclude.push(item.tag)
            else
                only.push(item.tag)
        }

        var dirs = ConfigBridge.dirs
        var picked = []
        var checked = 0

        for (var j = 0; j < dirs.length; j++) {
            if (isDirChecked(dirs[j].path)) {
                picked.push(dirs[j].path)
                checked++
            }
        }

        // 有目录可选但一个都没勾 -> 什么都不显示（core 把空列表理解成"不限目录"）
        if (dirs.length > 0 && checked === 0)
            picked = [noDirSentinel]

        return ConfigBridge.search(include, exclude, only, picked)
    }

    // 列表选中项（标签库页用
    property int selectedTypeId: -1
    property int selectedTagId: -1

    // 新建类型时的默认颜色：界面默认值
    property string typeColor: "#ffb3c6"

    // 同步页：输入 / 选择态
    // 「服务器名称」输入框与它双向绑定
    // 默认名 tagmeow：留空时桥也会兜底成这个名字
    property string serverName: "tagmeow"

    // 客户端面板选中的设备行（-1 = 一台都没选） 设备列表本身由桥下行
    property int selectedServerIndex: -1

    // 选中某台设备（纯 UI 态）：点行 / 点该行的下载按钮都走这里
    // 注意：这个函数必须存在 —— 页面里是 "Store.selectServer(i); ConfigBridge.startDownload(i)"
    function selectServer(index) {
        selectedServerIndex = index
    }

    // 桥接层的提示 -> Toast
    // 字号 / 主题 / 语言的真值都在 core（./config/config.json），由 ConfigBridge 暴露给 QML
    // 本文件不存第二份：Theme.baseFontSize 读 ConfigBridge.fontSize Lang.current 读
    // ConfigBridge.language 设置页直接写 ConfigBridge.theme / 调 ConfigBridge.setLanguage
    // 桥里失败时统一发 errorOccurred，这里转成 toastRequested 由 Main.qml 弹 Toast
    readonly property Connections bridgeErrorRelay: Connections {
        target: ConfigBridge

        function onErrorOccurred(message) {
            store.toast(message)
        }

        // 同步过程提示：桥只给文案键 + 一个参数 措辞留在 Lang.qml 一份（Lang.t 取不到键会原样返回键名）
        function onSyncMessage(messageId, arg) {
            store.toast(Lang.t(messageId, arg))
        }

        // 标签库操作提示（类型/标签的增删改）：同一套「文案键 + 参数」
        function onLibraryMessage(messageId, arg) {
            store.toast(Lang.t(messageId, arg))
        }
    }
}
