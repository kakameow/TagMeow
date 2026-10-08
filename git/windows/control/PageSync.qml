// 同步页：服务端共享 + 客户端下载
// 参考稿 #page-sync（HTML 1375-1443 行）与 CSS .sync-layout / .sync-panel /
// .sync-form-row / .input-field / .queue-header / .queue-list / .queue-item / .server-list（540-568 行）
// 数据全部来自 Store（core 桥接层），本页不再有任何假数据 / 假提示：
//   * 发送队列 = Store.serverQueue（core 的待发送目录快照），入队走「选择目录」-> Store.enqueueServerDir(index)
//   * 设备列表 = Store.clientServers（core 扫到的局域网设备），选中行 = Store.selectedServerIndex
//   * 每个按钮都落到 core 的真实接口（见 page.runAction / 行内「下载」按钮），没有"只弹一句按钮名"的占位提示
// 固定尺寸版：页面根节点 = 内容区可用高度 740，两个面板固定高度平分，列表内部滚动（滚动条 AlwaysOff）
// 测试阶段残留已清：两个 ListModel 初始为空（不再有 Project Aurora / TagMeow-Server 假数据），
// 列表头的「＋ 添加」按钮与其 addClicked 逻辑已删；列表为空时在框内显示灰色提示（暂无任务 / 未发现服务端）
// 行内 ✕ 已删：core 没有"把某条目录移出队列""从扫描结果里删一台设备"的接口
//（停服 / 断开客户端才会清队列，设备列表靠重新扫描刷新），留着它只能假成功，所以直接去掉
//
// 字号说明：参考稿的 11.5 / 10.5px 在 Qt 6.11 里写不了字面量（font.pixelSize 是整型，
// 会报 Invalid property assignment: int expected），所以统一向下取整，注释里保留原值。

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {
    id: page

    // ---------------- 固定尺寸（主壳窗口锁定 1280×820，内容区不再整页滚动） ----------------
    // 内容区可用高度 = 820 - 顶栏 52 - 内容区上下内边距 14*2 = 740
    readonly property int pageH: 740
    readonly property int gap: 12          // 页头与面板组之间的间距
    readonly property int panelGap: 10     // 两个面板之间的间距（.sync-layout gap）
    // 两个面板平分「余下高度」；页头高度直接取 PageHeader 的真实高度，
    // 这样字号/字体度量有变化时也不会把页头裁掉，最多底部差几个像素
    readonly property int panelH: Math.floor((pageH - headerBox.height - gap - panelGap) / 2)
    // 面板内除列表之外的高度：标题 21 + 表单行 30*2 + 列表头 26 + 4 段间距 10*4 + 上下内边距 14*2 = 175
    readonly property int panelChromeH: 21 + 30 + 30 + 26 + 10 * 4 + 14 * 2
    // 列表固定可视高度（约 152）：内容超出时只在列表内部滚动
    readonly property int listH: Math.max(34, panelH - panelChromeH)

    implicitHeight: pageH
    // 参考稿断点：<760 时表单行纵向、输入框整宽、按钮换行
    readonly property bool narrow: width < 760

    // ---------------- 本页私有数据（数据源都是 Store，这里只做"快照 -> 列表模型"的搬运） ----------------
    // 发送队列：内容 = Store.serverQueue（core 的待发送目录），为空时列表里显示灰色提示
    ListModel {
        id: sendModel
    }

    // 扫描到的服务端：内容 = Store.clientServers（core 扫到的局域网设备）
    ListModel {
        id: serverModel
    }

    // ---------------- core 快照 -> 本页列表模型 ----------------
    // core 的 serverQueue() / scanServers() 都是"调一次拿一次"的普通调用，
    // Store 里已经把结果缓存成 serverQueue / clientServers，这里只把快照搬进 ListModel
    function syncSendQueue() {
        sendModel.clear()
        for (var i = 0; i < Store.serverQueue.length; i++) {
            var dir = Store.serverQueue[i]
            // title = 目录名（core 给的规范名），status = 目录绝对路径
            sendModel.append({ title: String(dir.name), status: String(dir.path) })
        }
    }

    function syncServerList() {
        serverModel.clear()
        for (var i = 0; i < Store.clientServers.length; i++) {
            var device = Store.clientServers[i]
            // 设备行显示 名字 + ip:port（core 只给这三样，端口拼在地址后面）
            serverModel.append({ name: String(device.name),
                                 address: String(device.ip) + ":" + String(device.port) })
        }
    }

    // 页面初始化先取一次快照（此时服务端没开、也没扫过，两份都是空列表 -> 直接显示空状态文案）
    Component.onCompleted: {
        page.syncSendQueue()
        page.syncServerList()
    }

    Connections {
        target: Store

        // 入队 / 停服 / 断开客户端后 core 会重新广播队列：重新搬一遍
        function onServerQueueChanged() {
            page.syncSendQueue()
        }

        // 扫描结果换了一批：行模型跟着重建（选中项由 Store.selectedServerIndex 决定）
        function onClientServersChanged() {
            page.syncServerList()
        }
    }

    // ---------------- 本页按钮的动作分发 ----------------
    // 两个面板的按钮都是数据行渲染出来的（FieldRow / QueueHeader 的 buttons 数组），
    // 所以"点的是哪个动作"由数据行里的 action 名字决定；每个分支都落到 Store / core 的真实接口上：
    // 没有任何分支只是弹一句提示（占位 toast 就是这次要清掉的东西）
    function runAction(action) {
        if (action === "toggleServer") {
            Store.toggleServer()
        } else if (action === "chooseDir") {
            dirPicker.open()
        } else if (action === "disconnectServerClient") {
            Store.disconnectServerClient()
        } else if (action === "scan") {
            Store.scanServers()
        } else if (action === "clearRecords") {
            Store.clearDownloadRecords()
        } else if (action === "disconnectClient") {
            Store.disconnectClient()
        }
    }

    // ---------------- 内联小组件 ----------------

    // .input-field：高 30 / 圆角 7 / 1px line2 边框，聚焦时边框变 accent
    component InputField: TextField {
        id: input

        height: 30
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: Theme.px(12)
        font.family: Theme.fontFamily
        color: Theme.text
        placeholderTextColor: Theme.text3
        selectByMouse: true

        background: Rectangle {
            radius: 7
            color: Theme.surface
            border.width: 1
            border.color: input.activeFocus ? Theme.accent : Theme.line2
        }
    }

    // .sync-form-row：label（宽 66、右对齐、12px/650）+ 输入框 + 右侧按钮
    // stacked 为真（<760）时改为：label 左对齐整宽 → 输入框整宽 → 按钮换行
    // 按钮本身不认识 Store / 本页控件：点下去只把数据行里的 action 名字交给页面（onActionClicked -> page.runAction）
    component FieldRow: Item {
        id: row

        property string labelText: ""
        property alias fieldText: field.text
        property string placeholder: ""
        property bool stacked: false
        // 输入框只读：路径类字段的值由 core 决定（core 没有对应 setter），不让用户敲进去假装生效
        property bool fieldReadOnly: false
        // 按钮描述数组：[{ text, kind, action, enabled, icon }]
        property var buttons: []

        signal actionClicked(string action)

        width: parent ? parent.width : 0
        height: stacked
                ? (rowLabel.height + 6 + field.height + (buttonRow.visible ? 8 + buttonRow.height : 0))
                : 30

        Text {
            id: rowLabel
            x: 0
            y: 0
            width: row.stacked ? row.width : 66
            height: row.stacked ? 17 : 30
            text: row.labelText
            horizontalAlignment: row.stacked ? Text.AlignLeft : Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: Theme.px(12)
            font.weight: Font.DemiBold
            font.family: Theme.fontFamily
            color: Theme.text
        }

        InputField {
            id: field
            x: row.stacked ? 0 : 76
            y: row.stacked ? rowLabel.height + 6 : 0
            width: row.stacked
                   ? row.width
                   : Math.max(80, row.width - 76 - (buttonRow.visible ? buttonRow.width + 10 : 0))
            placeholderText: row.placeholder
            readOnly: row.fieldReadOnly
        }

        Row {
            id: buttonRow
            anchors.right: parent.right
            y: row.stacked ? field.y + field.height + 8 : (row.height - height) / 2
            spacing: 6
            visible: row.buttons.length > 0

            Repeater {
                model: row.buttons

                delegate: Btn {
                    required property var modelData

                    text: modelData.text
                    kind: modelData.kind ? modelData.kind : "secondary"
                    small: modelData.small === true
                    iconSource: modelData.icon ? modelData.icon : ""
                    // 没有 action 名字的按钮（core 里没有对应接口）一律置灰：不做"点了像成功"的假按钮
                    enabled: modelData.enabled !== false && !!modelData.action
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: row.actionClicked(modelData.action ? modelData.action : "")
                }
            }
        }
    }

    // .queue-header：左侧小标题 + 右侧动作按钮（gap 5，窄屏折行）
    // 原来的「＋ 添加」测试按钮已删（含 addClicked 信号与 add 分支）
    // 按钮行为同样只把 action 名字交给页面：onActionClicked -> page.runAction
    component QueueHeader: Item {
        id: header

        property string titleText: ""
        property bool stacked: false
        // 按钮描述数组：[{ text, kind, action, enabled, icon }]
        property var buttons: []

        signal actionClicked(string action)

        width: parent ? parent.width : 0
        height: stacked ? titleLabel.height + 6 + actionRow.height : 26

        Text {
            id: titleLabel
            x: 0
            y: 0
            height: 17
            text: header.titleText
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Theme.px(12)
            font.weight: Font.Bold
            font.family: Theme.fontFamily
            color: Theme.text
        }

        Row {
            id: actionRow
            anchors.right: parent.right
            y: header.stacked ? titleLabel.height + 6 : (header.height - height) / 2
            spacing: 5

            Repeater {
                model: header.buttons

                delegate: Btn {
                    required property var modelData

                    text: modelData.text
                    kind: modelData.kind ? modelData.kind : "secondary"
                    small: true
                    iconSource: modelData.icon ? modelData.icon : ""
                    // 没有 action 名字的按钮（core 里没有对应接口）一律置灰：不做"点了像成功"的假按钮
                    enabled: modelData.enabled !== false && !!modelData.action
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: header.actionClicked(modelData.action ? modelData.action : "")
                }
            }
        }
    }

    // ---------------- 页面内容 ----------------
    Column {
        id: pageCol
        width: parent.width
        height: page.pageH        // 固定高度：不再由内容撑高，也不在底部留空白
        spacing: page.gap

        PageHeader {
            id: headerBox
            width: parent.width
            stacked: page.narrow
            eyebrow: Lang.t("sync.eyebrow")
            title: Lang.t("nav.sync")
            desc: Lang.t("sync.desc")
        }

        // .sync-layout：上下两个面板，间距 10；面板组高度固定，两块平分
        Column {
            id: panels
            width: parent.width
            height: page.panelH * 2 + page.panelGap
            spacing: page.panelGap

            // ==================== 面板一：服务端 ====================
            Rectangle {
                id: serverPanel
                width: parent.width
                height: page.panelH      // 固定高度（不再由内容撑高）
                clip: true               // 极端窄屏表单行折行时，内容不画到面板之外
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: serverCol
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 10

                    // 面板标题
                    Text {
                        x: 0
                        width: parent.width
                        height: 21
                        text: Lang.t("sync.server_panel")
                        verticalAlignment: Text.AlignTop
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    // 表单行：服务器名称 + 启动/关闭
                    // 与 Store.serverName 双向绑定：Store 一变输入框跟着变，用户敲进去的内容立刻写回 Store，
                    // 启动按钮就是拿这个名字去 Core.startServer(Store.serverName)
                    //（名字留空时原样把空串交 core，core 自己会兜底成默认应用名）
                    FieldRow {
                        id: serverNameRow
                        labelText: Lang.t("sync.server_name_label")
                        placeholder: Lang.t("sync.serverName")
                        stacked: page.narrow
                        // 用户输入会打断 fieldText 这条绑定，之后靠下面的 handler 单向写回 Store；
                        // 两边最终都以 Store.serverName 为准，值相同时不会再触发变更，不会来回弹
                        fieldText: Store.serverName
                        onFieldTextChanged: Store.serverName = fieldText
                        onActionClicked: (action) => page.runAction(action)
                        buttons: [{
                            text: Store.serverRunning ? Lang.t("sync.stop") : Lang.t("sync.start"),
                            kind: Store.serverRunning ? "danger" : "primary",
                            action: "toggleServer",
                            icon: Store.serverRunning ? "qrc:/img/power-off.svg" : "qrc:/img/power.svg"
                        }]
                    }

                    // 表单行：共享目录（只读回显最近一次入队的目录；真正的入队走右边的「选择目录」弹窗）
                    FieldRow {
                        id: dirRow
                        labelText: Lang.t("sync.share_dir_label")
                        placeholder: Lang.t("sync.dirPath")
                        stacked: page.narrow
                        fieldReadOnly: true
                        onActionClicked: (action) => page.runAction(action)
                        buttons: [{ text: Lang.t("sync.choose_dir"), kind: "secondary", action: "chooseDir",
                                    icon: "qrc:/img/folder.svg" }]
                    }

                    // 队列头：发送队列 + 断开客户端连接（「＋ 添加」测试按钮已删）
                    QueueHeader {
                        titleText: Lang.t("sync.send_queue")
                        stacked: page.narrow
                        onActionClicked: (action) => page.runAction(action)
                        buttons: [
                            { text: Lang.t("sync.disconnect_client"), kind: "danger",
                              action: "disconnectServerClient",
                              icon: "qrc:/img/unplug.svg" }
                        ]
                    }

                    // .queue-list：浅底，固定可视高度 + 内部竖向滚动
                    Rectangle {
                        id: sendListBox
                        width: parent.width
                        height: page.listH       // 固定尺寸（原 Math.min(160, ...) 改为固定值）
                        radius: 8
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.line
                        clip: true

                        // 空列表提示（列表里有内容时被 ListView 盖住，不参与交互）
                        Text {
                            anchors.centerIn: parent
                            visible: sendModel.count === 0
                            text: Lang.t("sync.queue_empty")
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                        }

                        ListView {
                            id: sendList
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: sendModel

                            // 细竖向滚动条（始终隐藏，滚轮/拖拽照常）
                            ScrollBar.vertical: ScrollBar {
                                id: sendBar
                                policy: ScrollBar.AlwaysOff
                                width: 7
                                padding: 0
                                background: Item { }

                                contentItem: Rectangle {
                                    implicitWidth: 7
                                    radius: 3.5
                                    color: sendBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                }
                            }

                            delegate: Item {
                                id: sendItem

                                required property string title
                                required property string status
                                required property int index

                                width: sendList.width
                                height: 34

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 11
                                    anchors.right: statusText.left
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: sendItem.title
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.px(11)        // CSS 11.5px
                                    font.family: Theme.fontFamily
                                    color: Theme.text
                                }

                                // 状态（.muted）：显示目录绝对路径（长了从中间省略）
                                Text {
                                    id: statusText
                                    anchors.right: parent.right
                                    anchors.rightMargin: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, Math.max(60, sendItem.width * 0.6))
                                    text: sendItem.status
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.px(10)        // CSS 10.5px（.muted）
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                }

                                // 原来的行尾 ✕（从队列里删掉这一条）已删：
                                // core 没有"单独移出队列一条目录"的接口（停服 / 断开客户端才会清空队列），
                                // 留着它只能弹一句假提示，所以直接去掉；队列快照由 serverQueueChanged 刷新

                                // 行间分隔线（最后一条没有）
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.line
                                    visible: sendItem.index < sendModel.count - 1
                                }
                            }
                        }
                    }
                }
            }

            // ==================== 面板二：客户端 ====================
            Rectangle {
                id: clientPanel
                width: parent.width
                height: page.panelH      // 固定高度（不再由内容撑高）
                clip: true               // 极端窄屏表单行折行时，内容不画到面板之外
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: clientCol
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 10

                    Text {
                        x: 0
                        width: parent.width
                        height: 21
                        text: Lang.t("sync.client_panel")
                        verticalAlignment: Text.AlignTop
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    // 表单行：保存路径（只读）
                    // core 既没有"改下载目录"的接口，也没有保存路径的 getter，所以这里：
                    //   * 输入框只留 placeholder 并置为只读 —— 不显示、也不接受一个从没被真正应用过的路径
                    //   * 右边的「更改保存目录 / 加入管理目录」两个按钮留位置但置灰（没有 action 名字 -> 不可点）
                    FieldRow {
                        labelText: Lang.t("sync.save_to")
                        placeholder: Lang.t("sync.save_path_placeholder")
                        stacked: page.narrow
                        fieldReadOnly: true
                        onActionClicked: (action) => page.runAction(action)
                        buttons: [
                            { text: Lang.t("sync.change_path"), kind: "secondary",
                              icon: "qrc:/img/folder-plus.svg" },
                            { text: Lang.t("sync.path_as_root"), kind: "secondary",
                              icon: "qrc:/img/folder.svg" }
                        ]
                    }

                    // 队列头：扫描到的服务端 + 扫描 / 清空下载记录 / 断开连接（「＋ 添加」测试按钮已删）
                    QueueHeader {
                        titleText: Lang.t("sync.found_servers")
                        stacked: page.narrow
                        onActionClicked: (action) => page.runAction(action)
                        buttons: [
                            { text: Lang.t("browse.search"), kind: "secondary", action: "scan",
                              icon: "qrc:/img/search.svg" },
                            { text: Lang.t("sync.clear_cache"), kind: "secondary", action: "clearRecords",
                              icon: "qrc:/img/trash.svg" },
                            { text: Lang.t("sync.disconnect"), kind: "danger", action: "disconnectClient",
                              icon: "qrc:/img/unplug.svg" }
                        ]
                    }

                    // .server-list：白底条目，固定可视高度 + 内部竖向滚动
                    Rectangle {
                        id: serverListBox
                        width: parent.width
                        height: page.listH       // 固定尺寸（原 Math.min(160, ...) 改为固定值）
                        radius: 8
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.line
                        clip: true

                        // 空列表提示（列表里有内容时被 ListView 盖住，不参与交互）
                        Text {
                            anchors.centerIn: parent
                            visible: serverModel.count === 0
                            text: Lang.t("sync.no_server")
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                        }

                        ListView {
                            id: serverList
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: serverModel

                            // 细竖向滚动条（始终隐藏，滚轮/拖拽照常）
                            ScrollBar.vertical: ScrollBar {
                                id: serverBar
                                policy: ScrollBar.AlwaysOff
                                width: 7
                                padding: 0
                                background: Item { }

                                contentItem: Rectangle {
                                    implicitWidth: 7
                                    radius: 3.5
                                    color: serverBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                }
                            }

                            delegate: Item {
                                id: serverItem

                                required property string name
                                required property string address
                                required property int index

                                width: serverList.width
                                height: page.narrow ? 86 : 52

                                // 行底色：选中的那台（Store.selectedServerIndex）用 accentSoft 标出来
                                Rectangle {
                                    anchors.fill: parent
                                    color: Store.selectedServerIndex === serverItem.index ? Theme.accentSoft : Theme.surface
                                }

                                // 选中态的左侧色条（只是视觉标记，不改变行高）
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    color: Theme.accent
                                    visible: Store.selectedServerIndex === serverItem.index
                                }

                                // 点整行 = 选中这一台（下载按钮在它上层，点按钮不会走到这里）
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Store.selectServer(serverItem.index)
                                }

                                Column {
                                    id: serverInfo
                                    anchors.left: parent.left
                                    anchors.leftMargin: 11
                                    anchors.top: parent.top
                                    anchors.topMargin: 9
                                    width: Math.max(0, parent.width - 22 - 110)
                                    spacing: 2

                                    Text {
                                        width: parent.width
                                        height: 17
                                        text: serverItem.name
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(12)
                                        font.weight: Font.DemiBold
                                        font.family: Theme.fontFamily
                                        color: Theme.text
                                    }

                                    Text {
                                        width: parent.width
                                        height: 15
                                        text: serverItem.address
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)        // CSS 10.5px（.muted）
                                        font.family: Theme.fontFamily
                                        color: Theme.text3
                                    }
                                }

                                // 下载（primary small）：下载的永远是"当前选中的设备"
                                //（先点这一行把它设为选中，再按 Store.selectedServerIndex 去下）
                                // 正在下载（Core.clientDownloading）时置灰，避免并发下第二台
                                Btn {
                                    id: downloadBtn
                                    anchors.right: parent.right
                                    anchors.rightMargin: 11
                                    anchors.verticalCenter: page.narrow ? undefined : parent.verticalCenter
                                    anchors.top: page.narrow ? serverInfo.bottom : undefined
                                    anchors.topMargin: page.narrow ? 8 : 0
                                    text: Lang.t("sync.download_action")
                                    kind: "primary"
                                    small: true
                                    iconSource: "qrc:/img/download.svg"
                                    enabled: !Store.clientDownloading
                                    opacity: enabled ? 1.0 : 0.5
                                    onClicked: {
                                        Store.selectServer(serverItem.index)
                                        Store.startDownload(Store.selectedServerIndex)
                                    }
                                }

                                // 原来的行尾 ✕（从扫描结果里删掉这台）已删：core 没有"删掉扫到的某台设备"的接口，
                                // 设备列表由 Store.scanServers() 的扫描结果决定；留着 ✕ 只能弹假提示

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.line
                                    visible: serverItem.index < serverModel.count - 1
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- 共享目录选择弹窗（「选择目录」按钮打开） ----------
    // 不引入新的文件对话框依赖：直接列 Store.dirs（core 已经管着、且已经建立索引的目录），
    // 选中一项 -> Store.enqueueServerDir(index)（服务端没开 / 队列里已有同一目录时，Store 会给出提示并拒绝）
    Popup {
        id: dirPicker

        anchors.centerIn: parent
        width: Math.min(380, page.width - 40)
        // 高度 = 上下内边距 24 + 标题 17 + 间距 8 + 列表（每行 38，最多 5 行）
        height: 24 + 17 + 8 + Math.max(1, Math.min(5, Store.dirs.length)) * 38
        modal: true
        padding: 0

        background: Rectangle {
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
        }

        contentItem: Item {
            Text {
                id: pickerTitle
                x: 12
                y: 12
                width: parent.width - 24
                height: 17
                text: Lang.t("sync.share_dir_label")
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Theme.px(12)
                font.weight: Font.Bold
                font.family: Theme.fontFamily
                color: Theme.text
            }

            // 还没加过受管目录：没有可入队的目录
            Text {
                x: 12
                y: 37
                width: parent.width - 24
                height: 38
                visible: Store.dirs.length === 0
                text: Lang.t("dir.empty")
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: Theme.px(11)
                font.family: Theme.fontFamily
                color: Theme.text3
            }

            ListView {
                id: pickerList
                x: 0
                y: 37
                width: parent.width
                height: parent.height - 37
                visible: Store.dirs.length > 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: Store.dirs

                delegate: Item {
                    id: pickerRow

                    required property var modelData
                    required property int index

                    width: pickerList.width
                    height: 38

                    Rectangle {
                        anchors.fill: parent
                        color: pickerArea.containsMouse ? Theme.hover : "transparent"
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            height: 17
                            text: pickerRow.modelData.name
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(12)
                            font.weight: Font.DemiBold
                            font.family: Theme.fontFamily
                            color: Theme.text
                        }

                        Text {
                            width: parent.width
                            height: 15
                            text: pickerRow.modelData.path
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(10)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                        }
                    }

                    MouseArea {
                        id: pickerArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 先把要入队的目录取出来（close() 之后列表可能已经变了，索引会失效）
                            var picked = Store.dirs[pickerRow.index]
                            // 入队成功才把路径回显到「共享目录」输入框：不成功不写，避免显示一个没进队列的目录
                            if (Store.enqueueServerDir(pickerRow.index) && picked)
                                dirRow.fieldText = String(picked.path)
                            dirPicker.close()
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.line
                        visible: pickerRow.index < Store.dirs.length - 1
                    }
                }
            }
        }
    }
}
