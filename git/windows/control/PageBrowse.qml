// 文件页（参考稿 #page-browse，1056-1236 行；CSS 见 165-461、892-996 行）
// 组成：页面头 → 筛选面板（包含 / 排除 / 仅看 + 目录多选 + 搜索 / 清空）→ 文件卡片 + 标签库卡片 → 底部提示
// 交互只做简单增删：筛选增删、目录勾选、文件行增删、拖拽或点击给文件赋值标签、标签小片移除
// 断点用页面自身宽度判断（1180 / 980 / 760），与参考稿 @media 一一对应
// 尺寸体系：主壳窗口锁定 1280×820，页面固定填满内容区（约 740 高），各块高度写死，
//           workspace 吃掉剩余高度，只有文件列表 / 标签库列表在卡片内部滚动（滚动条隐藏）
// 已知近似：参考稿的 box-shadow 一律用 1px 边框代替；视图切换只改按钮样式，不做真实网格布局

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {
    id: page

    // ---------- 断点与公共尺寸 ----------
    readonly property bool stackFilter: width < 1180        // <1180：筛选面板改纵向（分组在上、多选与按钮在下）
    readonly property bool stackRail: width < 980           // <980：标签库卡片整宽落到文件卡片下方
    readonly property bool compact: width < 760             // <760：页面头纵向、隐藏 drop-target
    readonly property bool showFileMeta: width >= 1180      // 文件行的大小 / 时间列
    readonly property bool showDropTarget: width >= 760
    readonly property int railWidth: stackFilter ? Theme.railNarrow : Theme.rail   // 240 / 264
    readonly property int cardGap: 10                       // .workspace gap
    readonly property int colGap: 10                        // .file-row gap
    readonly property int actionsHeight: 72                 // 多选 36 + 间隔 6 + 按钮 30（= 参考稿右列内容高度）
    readonly property int dirOptionHeight: 28               // .ms-option{min-height:28px}

    // ---------- 固定高度分配（页面高 ≈ 740，全部写死，不跟内容走）----------
    readonly property int fixedHeight: 740                  // 窗口 820 − 顶栏 52 − 上下留白 28
    readonly property int contentSpacing: 12                // 页内各块之间的间距（pageColumn.spacing）
    readonly property int cardHeadHeight: 42                // 卡片头高度（文件 / 标签库一致）
    readonly property int cardLineHeight: 1                 // 卡片头下方 1px 分隔线
    readonly property int footerHeight: 18                  // 底部提示条固定高度
    // 筛选面板固定高度：宽屏 116（10 内边距 + 行 16 + 8 + 右列 72 + 10）、窄屏纵向排布 248
    readonly property int filterPanelHeight: stackFilter ? 248 : 116
    // workspace 吃掉「页头 + 筛选面板 + 提示条」之外的剩余高度：两块卡片等高、固定，只有列表内部滚动
    // 页头高度取 PageHeader 实测值（约 62），避免写死后底部留白
    readonly property int workspaceHeight: Math.max(240, page.height - pageHeader.height - filterPanelHeight
                                                         - footerHeight - contentSpacing * 3)

    // 文件卡片视图切换（参考稿只有样式，这里只切 active 样式 + toast 说明）
    property bool gridView: false

    // 层级模式右键菜单的目标条目：菜单整页只声明一份，弹出前把目标塞进这两个属性
    property string menuPath: ""
    property bool menuIsDir: false

    // 从拖拽里取出标签名 —— 与源工程 TagContainer 的 onDropped 取法完全一致：
    // 内部拖拽（拖的是 TagPill）直接读来源元素的 tagText / tagColor；平台级拖拽再退回 mimeData
    function tagFromDrop(drop) {
        var src = drop.source
        if (src && typeof src.tagText === "string" && typeof src.tagColor === "string")
            return src.tagText
        var text = drop.getDataAsString("text/plain")
        return text ? String(text) : ""
    }

    // 标签库卡片折叠开关：折叠后只保留卡片头一行
    property bool railCollapsed: false

    // 标签库搜索框的输入（按名字过滤标签小片；空串 = 不过滤）
    property string tagQuery: ""

    // ---------------- 可拖拽标签小片 ----------------
    // 拖拽机制照搬源工程 control/TagRectangle.qml：手动跟手移动 + 按下临时改挂到窗口顶层 + 松手 Drag.drop()。
    // 用内联组件（component X: ...）而不是单独文件：不依赖 CMake/qmldir 注册，改完重新编译就能用。
    component TagDragPill: Rectangle {
        id: pillRoot

        property string tagText: ""
        property color tagColor: "#FFB6C1"
        signal plusClicked()
        signal exitedParent()

        // 按下时记录的原位置 / 原父级 / 原 z（源工程 TagRectangle 同名字段）
        property point initialPosition: Qt.point(0, 0)
        property bool _wasOutside: false
        property Item _originalParent: null
        property int _originalZ: 0
        property Item _dragContainer: null

        width: pillRow.implicitWidth + 28
        height: 22
        radius: 6
        color: Theme.surface
        border.width: 1
        border.color: pillMouse.containsMouse ? Theme.controlLine : Theme.line2

        // ---- 拖拽（源工程同款：Drag.keys = ["tag"]，落点不按 mime 过滤）----
        Drag.active: pillMouse.dragging
        Drag.keys: ["tag"]
        Drag.hotSpot.x: pillMouse.pressOffset.x
        Drag.hotSpot.y: pillMouse.pressOffset.y
        Drag.mimeData: ({
            "text/plain": pillRoot.tagText,
            "text/color": String(pillRoot.tagColor)
        })
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction

        // 没被接收时滑回原位
        ParallelAnimation {
            id: returnAnimation

            NumberAnimation {
                id: animX
                target: pillRoot
                property: "x"
                duration: 200
                easing.type: Easing.OutQuad
            }

            NumberAnimation {
                id: animY
                target: pillRoot
                property: "y"
                duration: 200
                easing.type: Easing.OutQuad
            }
        }

        Component.onCompleted:
        Row {
            id: pillRow
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5

            Rectangle {
                width: 7
                height: 7
                radius: 3.5
                color: pillRoot.tagColor
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pillRoot.tagText
                font.pixelSize: Theme.px(10)
                font.family: Theme.fontFamily
                color: Theme.text2
            }
        }

        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            cursorShape: Qt.OpenHandCursor

            property point pressOffset: Qt.point(0, 0)
            property bool dragging: false

            onPressed: (mouse) => {
                returnAnimation.stop()

                pillRoot.initialPosition = Qt.point(pillRoot.x, pillRoot.y)
                pillRoot._dragContainer = findContainer()
                pressOffset = Qt.point(mouse.x, mouse.y)
                dragging = true

                // 临时改挂到窗口顶层 Item：拖动中不被父容器裁剪（源工程做法）
                var top = findTopItem()

                if (top) {
                    pillRoot._originalParent = pillRoot.parent
                    pillRoot._originalZ = pillRoot.z
                    var pos = top.mapFromItem(pillRoot, 0, 0)
                    pillRoot.parent = top
                    pillRoot.x = pos.x
                    pillRoot.y = pos.y
                    pillRoot.z = 10000
                }
            }

            onPositionChanged: (mouse) => {
                if (!dragging) {
                    return
                }

                pillRoot.x = pillRoot.x + (mouse.x - pressOffset.x)
                pillRoot.y = pillRoot.y + (mouse.y - pressOffset.y)
                checkIfFullyOutside()
            }

            onReleased: (mouse) => {
                // 显式投递：由光标下的 DropArea 处理（源工程同款）
                pillRoot.Drag.drop()
                dragging = false

                if (pillRoot._originalParent) {
                    pillRoot.parent = pillRoot._originalParent
                    pillRoot.z = pillRoot._originalZ
                    pillRoot._originalParent = null
                }

                if (pillRoot._wasOutside) {
                    pillRoot.exitedParent()
                    pillRoot._wasOutside = false
                    pillRoot._dragContainer = null
                    return
                }

                animX.to = pillRoot.initialPosition.x
                animY.to = pillRoot.initialPosition.y
                returnAnimation.start()

                pillRoot._wasOutside = false
                pillRoot._dragContainer = null
            }

            // 是否已经完全位于所属落点容器之外
            function checkIfFullyOutside() {
                var container = pillRoot._dragContainer

                if (!container) {
                    return
                }

                var topLeft = pillRoot.mapToItem(container, 0, 0)
                var tagRect = Qt.rect(topLeft.x, topLeft.y, pillRoot.width, pillRoot.height)
                var containerRect = Qt.rect(0, 0, container.width, container.height)

                pillRoot._wasOutside = !rectsIntersect(tagRect, containerRect)
            }

            function rectsIntersect(r1, r2) {
                return r1.x < r2.x + r2.width && r2.x < r1.x + r1.width &&
                       r1.y < r2.y + r2.height && r2.y < r1.y + r1.height
            }

            // 向上查找"落点容器"：带 dropContainer 标记的祖先（源工程里是带 containerName 的 TagContainer）
            function findContainer() {
                var obj = pillRoot.parent

                while (obj) {
                    if (obj.hasOwnProperty("dropContainer")) {
                        return obj
                    }

                    obj = obj.parent
                }

                return null
            }

            // 向上查找窗口顶层 Item（z 属性存在的最外层），作为拖拽时的临时父级
            function findTopItem() {
                var obj = pillRoot.parent
                var top = null

                while (obj) {
                    if (typeof obj.z !== "undefined") {
                        top = obj
                    }

                    obj = obj.parent
                }

                return top
            }
        }

        // 右上角「＋」：把标签加进「包含」筛选
        Rectangle {
            x: pillRoot.width - 14
            y: 3
            width: 11
            height: 11
            radius: 3
            color: plusMouse.containsMouse ? Theme.accentSoft : "transparent"
            border.width: plusMouse.containsMouse ? 1 : 0
            border.color: Theme.chipBlueLine

            Text {
                anchors.centerIn: parent
                text: "＋"
                font.pixelSize: Theme.px(9)
                font.family: Theme.fontFamily
                color: plusMouse.containsMouse ? Theme.accentText : Theme.text3
            }

            MouseArea {
                id: plusMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: pillRoot.plusClicked()
            }
        }
    }

    // 切页保留滚动位置：页面在 StackLayout 里一直活着，ListView 也就不会重建，这里什么都不用做。
    // 只有「内容身份」变了才回到顶部 —— 换一批搜索结果、进出目录层级、返回上一层
    Connections {
        target: Store

        function onFilesChanged() { fileListHost.positionViewAtBeginning() }
        function onEntriesChanged() { fileListHost.positionViewAtBeginning() }
        function onBrowsePathChanged() { fileListHost.positionViewAtBeginning() }
    }

    // 搜索框里真的写了东西（去掉首尾空格）才算在过滤：空串 / 只有空格 = 不做任何过滤
    readonly property bool tagSearchActive: tagQuery.trim().length > 0

    // 标签库里按名字过滤：大小写不敏感的子串匹配；一次只查一个类型下的标签
    function tagsMatchingQuery(typeId) {
        var all = Store.tagsOfType(typeId)
        var query = tagQuery.trim().toLowerCase()
        if (query.length === 0)
            return all
        var out = []
        for (var i = 0; i < all.length; i++) {
            var name = all[i].name === undefined ? "" : String(all[i].name)
            if (name.toLowerCase().indexOf(query) >= 0)
                out.push(all[i])
        }
        return out
    }

    // 搜索词一个标签都没匹配上（标签库里显示「没有匹配的标签」；此时类型卡片全被隐藏）
    readonly property bool tagSearchEmpty: {
        if (!tagSearchActive)
            return false
        for (var i = 0; i < Store.types.length; i++) {
            if (tagsMatchingQuery(Store.types[i].typeId).length > 0)
                return false
        }
        return true
    }

    // 页面根节点固定填满内容区（StackLayout 用 implicitHeight，运行期用父高），不再跟内容走高
    implicitHeight: page.fixedHeight
    height: parent ? parent.height : page.fixedHeight

    Column {
        id: pageColumn
        width: page.width
        height: page.height
        spacing: page.contentSpacing   // .page-head{margin-bottom:12px}

        // ---------------- 页面头 ----------------
        PageHeader {
            id: pageHeader
            width: pageColumn.width
            eyebrow: Lang.t("browse.eyebrow")
            title: Lang.t("browse.files")
            desc: Lang.t("browse.desc")
            stacked: page.compact
        }

        // （层级浏览不再单独占一块：它与平铺搜索结果共用下面的文件卡片，只把列表 model 换成 Store.viewRows）

        // ---------------- 筛选面板（搜索时一直可见：层级浏览在它下面，与搜索结果共用同一块列表区）----------------
        Rectangle {
            id: filterPanel
            width: pageColumn.width
            height: page.filterPanelHeight              // 固定高度（宽屏 116 / 窄屏 248），不再跟内容走
            radius: Theme.radiusCard
            color: Theme.surface
            border.width: 1
            border.color: Theme.line

            Column {
                id: filterColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 10
                spacing: 8                              // .filter-top{margin-bottom:8px}

                // 顶行：当前筛选 + 摘要
                Item {
                    width: parent.width
                    height: 16

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Lang.t("browse.current_filter")
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Store.filterSummary()
                        font.pixelSize: Theme.px(10)
                        font.family: Theme.fontFamily
                        color: Theme.text3
                    }
                }

                // 分组 + 右列（宽屏左右排，窄屏上下排）
                Item {
                    id: filterBody
                    x: 12
                    width: parent.width - 24                // .filter-layout{padding:0 12px}
                    height: page.stackFilter ? (groupsHost.height + 12 + page.actionsHeight)
                                             : Math.max(groupsHost.height, page.actionsHeight)

                    // --- 左：包含 / 排除 / 仅看 ---
                    Item {
                        id: groupsHost
                        x: 0
                        y: 0
                        width: parent.width
                        height: page.stackFilter ? (groupBoxHeight * 3 + 12) : page.actionsHeight

                        // 宽屏时三组高度对齐右列（参考稿 flex 的 stretch），窄屏每组 36（min-height:36px）
                        readonly property int groupBoxHeight: page.stackFilter ? 36 : page.actionsHeight

                        Repeater {
                            model: ["include", "exclude", "only"]

                            delegate: Rectangle {
                                id: groupBox

                                required property string modelData
                                required property int index
                                readonly property string kind: modelData

                                // 本组筛选条件（带上它在 Store.filters 里的真实下标，删除时要用）
                                readonly property var items: {
                                    var list = []
                                    for (var i = 0; i < Store.filters.length; i++) {
                                        if (Store.filters[i].kind === kind)
                                            list.push({ index: i, tag: Store.filters[i].tag, color: Store.filters[i].color })
                                    }
                                    return list
                                }

                                // 宽屏横排三等分，窄屏整宽纵向堆叠（对应 .filter-group flex 1 1 0 / 1 1 100%）
                                x: page.stackFilter ? 0 : index * ((groupsHost.width + 6) / 3)
                                y: page.stackFilter ? index * (height + 6) : 0
                                width: page.stackFilter ? groupsHost.width : (groupsHost.width - 12) / 3
                                height: groupsHost.groupBoxHeight
                                radius: 8
                                color: Theme.filterGroupBg[groupBox.kind]
                                // 拖标签悬停在某一组上时用「粗深色描边」标出目标组：
                                // Theme.text 是近黑的正文色，夜间主题里是浅色，两边都够对比度
                                border.width: groupDrop.containsDrag ? 2 : 1
                                border.color: groupDrop.containsDrag ? Theme.text : Theme.filterGroupLine[groupBox.kind]

                                // 拖标签进这一组 = 直接加进对应的筛选条件（与文件行拖拽同一套取数逻辑）
                                // 不写 keys：源工程 TagContainer 的 DropArea 也没写，空 keys = 接受任何拖拽；
                                // 写了 ["text/plain"] 会去和 Drag.keys（"tag"）比对，反而把内部拖拽全挡掉
                                DropArea {
                                    id: groupDrop
                                    anchors.fill: parent

                                    onDropped: (drop) => {
                                        drop.accepted = true
                                        var tag = page.tagFromDrop(drop)
                                        if (tag.length > 0)
                                            Store.addFilter(groupBox.kind, tag)
                                    }
                                }

                                // 组标题图标（funnel / funnel-plus / funnel-x）+ 组名
                                Image {
                                    id: groupIcon
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14
                                    height: 14
                                    source: groupBox.kind === "include" ? "qrc:/img/funnel-plus.svg"
                                          : (groupBox.kind === "exclude" ? "qrc:/img/funnel-x.svg" : "qrc:/img/funnel.svg")
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }

                                Text {
                                    id: groupLabel
                                    anchors.left: groupIcon.right
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: groupBox.kind === "include" ? Lang.t("cnt.include")
                                          : (groupBox.kind === "exclude" ? Lang.t("cnt.exclude") : Lang.t("cnt.only"))
                                    font.pixelSize: Theme.px(10)
                                    font.weight: Font.Bold
                                    font.family: Theme.fontFamily
                                    color: Theme.text2
                                }

                                // 本组的 chip（横向滚动：拖拽 flick + 竖向滚轮 + 细滚动条）
                                // 组末尾的「＋」测试按钮已删，chip 区直接铺到组右内边距
                                Flickable {
                                    id: chipsFlick
                                    anchors.left: groupLabel.right
                                    anchors.leftMargin: 5
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 22
                                    contentWidth: chipsRow.width
                                    contentHeight: chipsRow.height
                                    clip: true
                                    flickableDirection: Flickable.HorizontalFlick
                                    boundsBehavior: Flickable.StopAtBounds

                                    // 竖向滚轮 → 横向滚动，clamp 到 [0, contentWidth - width]
                                    WheelHandler {
                                        onWheel: (event) => {
                                            var maxX = Math.max(0, chipsFlick.contentWidth - chipsFlick.width)
                                            if (maxX > 0) {   // chip 没溢出就不吃掉滚轮事件，交给外层页面滚动
                                                // 有横向滚轮分量时优先用它（少数鼠标 / 触控板），否则用竖向滚轮
                                                var delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                                                chipsFlick.contentX = Math.max(0, Math.min(maxX, chipsFlick.contentX + delta))
                                                event.accepted = true
                                            }
                                        }
                                    }

                                    // 细横向滚动条：3px、#cfd5dd（参考稿 .filter-group::-webkit-scrollbar）
                                    ScrollBar.horizontal: ScrollBar {
                                        id: chipsBar
                                        policy: ScrollBar.AlwaysOff
                                        height: 3
                                        padding: 0
                                        background: Item { }

                                        contentItem: Rectangle {
                                            implicitHeight: 3
                                            radius: 1.5
                                            color: chipsBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                        }
                                    }

                                    Row {
                                        id: chipsRow
                                        // chip 排布的真实总宽：保证 contentWidth 能大于容器宽，chip 溢出时才滚得动
                                        width: childrenRect.width
                                        height: chipsFlick.height
                                        spacing: 5

                                        Repeater {
                                            model: groupBox.items

                                            delegate: Chip {
                                                required property var modelData
                                                height: 22
                                                text: modelData.tag
                                                dotColor: modelData.color
                                                removable: true
                                                onRemoved: Store.removeFilter(modelData.index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- 右：目录多选 + 搜索 / 清空 ---
                    Item {
                        id: actionsHost
                        x: page.stackFilter ? 0 : (filterBody.width - page.railWidth)
                        y: page.stackFilter ? (groupsHost.height + 12) : 0
                        width: page.stackFilter ? filterBody.width : page.railWidth
                        height: page.actionsHeight

                        // 折叠触发器：▱ 摘要 ▼
                        Rectangle {
                            id: msTrigger
                            x: 0
                            y: 0
                            width: parent.width
                            height: 36
                            radius: 8
                            color: dirPopup.opened ? Theme.surface : (msTriggerMouse.containsMouse ? Theme.surfaceInsetHover : Theme.surfaceInset)
                            border.width: 1
                            border.color: dirPopup.opened ? Theme.accentLineSoft : Theme.lineSoft

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 7

                                Text {
                                    text: "▱"
                                    font.pixelSize: Theme.px(12)
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: Store.dirSummary()
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.px(12)
                                    font.weight: Font.DemiBold
                                    font.family: Theme.fontFamily
                                    color: Theme.text
                                }

                                Text {
                                    text: "▼"
                                    font.pixelSize: Theme.px(9)
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                    Layout.alignment: Qt.AlignVCenter
                                    rotation: dirPopup.opened ? 180 : 0

                                    Behavior on rotation {
                                        NumberAnimation { duration: 180 }
                                    }
                                }
                            }

                            MouseArea {
                                id: msTriggerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dirPopup.opened ? dirPopup.close() : dirPopup.open()
                            }
                        }

                        // 展开面板：用 Popup 定位在触发器下方，点击外部 / Esc 自动收起
                        Popup {
                            id: dirPopup
                            parent: msTrigger
                            x: 0
                            y: msTrigger.height + 4
                            width: msTrigger.width
                            padding: 5                                   // .ms-panel{padding:5px}
                            // max-height:220px；行高 28 + 行间隔 1
                            implicitHeight: Math.min(220, Store.dirChoices.length * (page.dirOptionHeight + 1) - 1 + 10)
                            height: implicitHeight
                            closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

                            background: Rectangle {
                                radius: 9
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.line2
                            }

                            contentItem: Flickable {
                                contentWidth: width
                                contentHeight: Store.dirChoices.length * (page.dirOptionHeight + 1) - 1
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds

                                Column {
                                    id: dirOptionColumn
                                    width: dirPopup.width - 10
                                    spacing: 1

                                    Repeater {
                                        model: Store.dirChoices

                                        delegate: Rectangle {
                                            required property var modelData
                                            required property int index
                                            width: dirOptionColumn.width
                                            height: page.dirOptionHeight
                                            radius: 6
                                            color: optionMouse.containsMouse ? Theme.surfaceInset : "transparent"

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 7
                                                anchors.rightMargin: 7
                                                spacing: 7

                                                // 自绘复选框
                                                Rectangle {
                                                    width: 14
                                                    height: 14
                                                    radius: 4
                                                    color: modelData.checked ? Theme.accent : Theme.surface
                                                    border.width: 1
                                                    border.color: modelData.checked ? Theme.accent : Theme.controlLine
                                                    Layout.preferredWidth: 14
                                                    Layout.preferredHeight: 14
                                                    Layout.alignment: Qt.AlignVCenter

                                                    Text {
                                                        anchors.centerIn: parent
                                                        visible: modelData.checked
                                                        text: "✓"
                                                        font.pixelSize: Theme.px(10)
                                                        font.family: Theme.fontFamily
                                                        color: "#ffffff"
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    Layout.alignment: Qt.AlignVCenter
                                                    text: modelData.label
                                                    elide: Text.ElideRight
                                                    font.pixelSize: Theme.px(12)
                                                    font.family: Theme.fontFamily
                                                    color: optionMouse.containsMouse ? Theme.text : Theme.text2
                                                }
                                            }

                                            MouseArea {
                                                id: optionMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Store.toggleDirChoice(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 搜索 / 清空
                        RowLayout {
                            x: 0
                            y: 42
                            width: parent.width
                            height: 30
                            spacing: 5

                            Btn {
                                text: Lang.t("browse.search")
                                font.pixelSize: Theme.px(11)
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                // 搜索 = 切回平铺搜索结果模式（层级浏览与搜索结果是同一块列表区的两个模式）
                                onClicked: {
                                    Store.exitBrowse()
                                    Store.runQuery()
                                    Store.toast(Lang.t("browse.search"))
                                }
                            }

                            Btn {
                                text: Lang.t("browse.clear")
                                font.pixelSize: Theme.px(11)
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                onClicked: {
                                    Store.exitBrowse()
                                    Store.clearFilters()
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------- 工作区：文件卡片 + 标签库卡片（层级浏览与平铺搜索结果在同一张文件卡片里切换）----------------
        Item {
            id: workspace
            width: pageColumn.width
            height: page.workspaceHeight                   // 固定吃掉上面各块之后的剩余高度
            readonly property int cardHeight: page.stackRail ? Math.floor((height - page.cardGap) / 2) : height

            // --- 文件卡片 ---
            Rectangle {
                id: filesCard
                x: 0
                y: 0
                width: page.stackRail ? workspace.width
                                      : Math.max(220, workspace.width - page.railWidth - page.cardGap)
                height: workspace.cardHeight   // 固定高度（与标签库卡片等高），内容超出由列表内部滚动
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.line
                clip: true

                Column {
                    id: filesCardColumn
                    width: parent.width
                    spacing: 0

                    // 卡片头：文件 + N 项｜≡ ▦
                    Item {
                        id: filesCardHead
                        width: parent.width
                        height: 42

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            height: 16
                            spacing: 6

                            Text {
                                text: Lang.t("browse.files")
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(12)
                                font.weight: Font.Bold
                                font.family: Theme.fontFamily
                                color: Theme.text
                            }

                            // 条数：平铺模式 = 搜索结果条数，层级模式 = 当前层条数（含最上面那条 "."），卡片头本身不变
                            Text {
                                text: Store.viewRowCount() + Lang.t("common.count_suffix")
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(10)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            // 视图切换（只切样式，不改真实布局）
                            Rectangle {
                                width: 54
                                height: 26
                                radius: 7
                                color: Theme.surfaceInsetHover
                                anchors.verticalCenter: parent.verticalCenter

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 2

                                    Rectangle {
                                        width: 24
                                        height: 22
                                        radius: 5
                                        color: page.gridView ? "transparent" : Theme.surface
                                        border.width: page.gridView ? 0 : 1
                                        border.color: Theme.line2

                                        Text {
                                            anchors.centerIn: parent
                                            text: "≡"
                                            font.pixelSize: Theme.px(11)
                                            font.family: Theme.fontFamily
                                            color: page.gridView ? Theme.text3 : Theme.textDeep
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (page.gridView) {
                                                    page.gridView = false
                                                    Store.toast(Lang.t("browse.view_list"))
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        width: 24
                                        height: 22
                                        radius: 5
                                        color: page.gridView ? Theme.surface : "transparent"
                                        border.width: page.gridView ? 1 : 0
                                        border.color: Theme.line2

                                        Text {
                                            anchors.centerIn: parent
                                            text: "▦"
                                            font.pixelSize: Theme.px(11)
                                            font.family: Theme.fontFamily
                                            color: page.gridView ? Theme.textDeep : Theme.text3
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (!page.gridView) {
                                                    page.gridView = true
                                                    Store.toast(Lang.t("browse.view_grid_hint"))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.line
                    }

                    // 文件列表（固定可视高度 = 卡片高 − 卡片头 42 − 分隔线 1，超出只在列表内部滚动）
                    // 层级浏览与搜索结果共用这一份列表：model 换成 Store.viewRows（层级模式多一条 "." 返回上层）
                    Item {
                        id: fileListArea
                        width: parent.width
                        height: filesCard.height - page.cardHeadHeight - page.cardLineHeight   // .file-list{padding:6px}

                        // 列表用 ListView（原来是 Column + Repeater，一次把所有行都建出来）：
                        // cacheBuffer 之外的行不实例化，搜索结果上千条时也只建可视的这几行
                        ListView {
                            id: fileListHost
                            // 列表本体按 .file-list{padding:6px} 内缩 6px：行左右各留 6px、上下各留 6px
                            // （Qt 6 的 ListView 不会把 leftMargin / topMargin 算进 delegate 定位，只能内缩列表本身）
                            x: 6
                            y: 6
                            width: fileListArea.width - 12
                            height: fileListArea.height - 12
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: Store.viewRows
                            spacing: 1                                       // .file-row + .file-row{margin-top:1px}
                            cacheBuffer: 400                                 // 可视范围外只多建这一小段

                            // 细竖向滚动条（按需出现）
                            ScrollBar.vertical: ScrollBar {
                                id: fileListBar
                                policy: ScrollBar.AlwaysOff
                                width: 7
                                padding: 0
                                background: Item { }

                                contentItem: Rectangle {
                                    implicitWidth: 7
                                    radius: 3.5
                                    color: fileListBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                }
                            }

                            delegate: Rectangle {
                                id: fileRow

                                required property var modelData
                                required property int index

                                readonly property bool metaVisible: page.showFileMeta
                                readonly property bool dropVisible: page.showDropTarget
                                readonly property int deleteWidth: 18
                                readonly property int fixedWidth: 32 + (metaVisible ? 76 : 0)
                                                                      + (dropVisible ? 88 : 0) + deleteWidth
                                readonly property int gapCount: 2 + (metaVisible ? 1 : 0) + (dropVisible ? 1 : 0)
                                readonly property int mainWidth: Math.max(80, width - 16 - fixedWidth - gapCount * page.colGap)

                                // 层级浏览（Store.browsePath 非空）时：目录行 / "." 行用 folder 图标，
                                // 行交互换成双击进入 + 右键菜单，行尾删除不出现 —— 其余渲染与搜索结果完全一致
                                readonly property bool levelRow: Store.browsePath.length > 0
                                readonly property bool dirRow: modelData.kind === "dir" || modelData.kind === "parent"

                                // 参考稿 .file-icon.img/.doc/.video/.plain：底色按类型，图标统一 16×16 svg
                                readonly property color iconBg: fileRow.dirRow ? Theme.tintGreen
                                                              : (modelData.iconKind === "img" ? Theme.tintBlue
                                                              : (modelData.iconKind === "doc" ? Theme.tintPurple
                                                              : (modelData.iconKind === "video" ? Theme.tintRed : Theme.surface3)))
                                // svg 里没有 doc / video 专属图标：目录用 folder，图片用 paint-roller，其余都用 file
                                readonly property string iconSource: fileRow.dirRow ? "qrc:/img/folder.svg"
                                                                     : (modelData.iconKind === "img"
                                                                        ? "qrc:/img/paint-roller.svg"
                                                                        : "qrc:/img/file.svg")

                                width: ListView.view.width                     // 列表已内缩 6px，行铺满列表宽度
                                height: Math.max(50, fileRowContent.height + 14)
                                radius: 8
                                color: fileDrop.containsDrag ? Theme.tintBlue
                                                             : (fileHover.containsMouse ? Theme.surface2 : Theme.surface)
                                border.width: 1
                                border.color: fileDrop.containsDrag ? Theme.dragLine
                                                                   : (fileHover.containsMouse ? Theme.lineSoft : "transparent")

                                // 行 hover（放在最底层，不挡行内的按钮）
                                MouseArea {
                                    id: fileHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }

                                // 层级模式的行交互（平铺模式这条 MouseArea 不可见，不接收鼠标，行为完全不变）：
                                // 双击 = "." 返回上一层 / 目录进入下一层 / 文件用系统方式打开；右键 = 弹菜单
                                MouseArea {
                                    anchors.fill: parent
                                    visible: fileRow.levelRow
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                                    onDoubleClicked: {
                                        if (fileRow.modelData.kind === "parent")
                                            Store.goUpLevel()
                                        else if (fileRow.dirRow)
                                            Store.enterDir(fileRow.modelData.path)
                                        else
                                            Store.openEntryInSystem(fileRow.modelData.path)
                                    }

                                    onPressed: (mouse) => {
                                        // "." 那条不响应右键（与源工程一致）
                                        if (mouse.button !== Qt.RightButton || fileRow.modelData.kind === "parent")
                                            return
                                        page.menuPath = fileRow.modelData.path
                                        page.menuIsDir = fileRow.dirRow
                                        var point = fileRow.mapToItem(page, mouse.x, mouse.y)
                                        levelMenu.x = point.x
                                        levelMenu.y = point.y
                                        levelMenu.open()
                                    }
                                }

                                // 拖拽落点：整行接收标签（层级模式按路径赋值，平铺模式按行号）
                                // 取标签名统一走 page.tagFromDrop（内部拖拽读 drop.source.tagText，平台级读 mimeData）
                                // 不写 keys：与源工程 TagContainer 的 DropArea 一致（空 keys 接受任何拖拽）
                                DropArea {
                                    id: fileDrop
                                    anchors.fill: parent

                                    onDropped: (drop) => {
                                        drop.accepted = true
                                        var tag = page.tagFromDrop(drop)
                                        if (tag.length === 0)
                                            return
                                        if (fileRow.levelRow)
                                            Store.assignTagByPath(fileRow.modelData.path, tag)
                                        else
                                            Store.assignTag(fileRow.index, tag)
                                    }
                                }

                                // 四列栅格：32px | 1fr | 76px | 88px（+ 行尾删除）
                                RowLayout {
                                    id: fileRowContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: Math.max(32, mainColumn.height)
                                    spacing: page.colGap

                                    // 1. 图标
                                    Rectangle {
                                        width: 32
                                        height: 32
                                        radius: 7
                                        color: fileRow.iconBg
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        Layout.alignment: Qt.AlignVCenter

                                        Image {
                                            anchors.centerIn: parent
                                            width: 18
                                            height: 18
                                            source: fileRow.iconSource
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                        }
                                    }

                                    // 2. 主信息：文件名 / 目录 / 标签小片
                                    Column {
                                        id: mainColumn
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: fileRow.mainWidth
                                        Layout.alignment: Qt.AlignVCenter
                                        spacing: 1

                                        Text {
                                            width: parent.width
                                            text: fileRow.modelData.fileName
                                            elide: Text.ElideRight
                                            font.pixelSize: Theme.px(12)
                                            font.weight: Font.DemiBold
                                            font.family: Theme.fontFamily
                                            color: Theme.text
                                        }

                                        Text {
                                            width: parent.width
                                            text: fileRow.modelData.dirPath
                                            elide: Text.ElideRight
                                            font.pixelSize: Theme.px(10)
                                            font.family: Theme.fontFamily
                                            color: Theme.text3
                                        }

                                        Item {
                                            width: 1
                                            height: 3
                                        }

                                        Flow {
                                            width: parent.width
                                            spacing: 4

                                            Repeater {
                                                model: fileRow.modelData.tags

                                                delegate: Rectangle {
                                                    id: tinyChip

                                                    required property var modelData

                                                    readonly property color toneBg: modelData.tone === "blue" ? Theme.chipBlueBg
                                                                                : (modelData.tone === "green" ? Theme.chipGreenBg : Theme.chipPlainBg)
                                                    readonly property color toneLine: modelData.tone === "blue" ? Theme.chipBlueLine
                                                                                  : (modelData.tone === "green" ? Theme.chipGreenLine : Theme.chipPlainLine)
                                                    readonly property color toneText: modelData.tone === "blue" ? Theme.chipBlueText
                                                                                  : (modelData.tone === "green" ? Theme.chipGreenText : Theme.chipPlainText)

                                                    width: tinyRow.implicitWidth + 12
                                                    height: 17
                                                    radius: 5
                                                    color: toneBg
                                                    border.width: 1
                                                    border.color: toneLine

                                                    Row {
                                                        id: tinyRow
                                                        anchors.centerIn: parent
                                                        spacing: 3

                                                        Text {
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            text: tinyChip.modelData.name
                                                            font.pixelSize: Theme.px(10)
                                                            font.family: Theme.fontFamily
                                                            color: tinyChip.toneText
                                                        }

                                                        Rectangle {
                                                            width: 10
                                                            height: 10
                                                            radius: 3
                                                            color: "transparent"
                                                            anchors.verticalCenter: parent.verticalCenter

                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: "✕"
                                                                font.pixelSize: Theme.px(8)
                                                                font.family: Theme.fontFamily
                                                                color: tinyChip.toneText
                                                            }

                                                            MouseArea {
                                                                anchors.fill: parent
                                                                cursorShape: Qt.PointingHandCursor
                                                                onClicked: {
                                                                    if (fileRow.levelRow)
                                                                        Store.removeTagByPath(fileRow.modelData.path, tinyChip.modelData.name)
                                                                    else
                                                                        Store.removeTagFromFile(fileRow.index, tinyChip.modelData.name)
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // 3. 大小 / 时间
                                    Column {
                                        visible: fileRow.metaVisible
                                        Layout.preferredWidth: 76
                                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                        spacing: 1

                                        Text {
                                            width: parent.width
                                            text: fileRow.modelData.sizeText
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: Theme.px(10)
                                            font.family: Theme.fontFamily
                                            color: Theme.text3
                                        }

                                        Text {
                                            width: parent.width
                                            text: fileRow.modelData.timeText
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: Theme.px(10)
                                            font.family: Theme.fontFamily
                                            color: Theme.text3
                                        }
                                    }

                                    // 4. drop-target：虚线框，点击 = 模拟拖入第一个标签
                                    Rectangle {
                                        id: dropTarget
                                        visible: fileRow.dropVisible
                                        Layout.preferredWidth: 88
                                        Layout.preferredHeight: 26
                                        Layout.alignment: Qt.AlignVCenter
                                        radius: 6
                                        color: fileDrop.containsDrag ? Theme.tintBlue : "transparent"

                                        Canvas {
                                            anchors.fill: parent
                                            property color strokeColor: fileDrop.containsDrag ? Theme.dragStroke : Theme.line2

                                            onStrokeColorChanged: requestPaint()

                                            onPaint: {
                                                var ctx = getContext("2d")
                                                ctx.clearRect(0, 0, width, height)
                                                ctx.strokeStyle = strokeColor
                                                ctx.lineWidth = 1
                                                ctx.setLineDash([4, 3])
                                                ctx.beginPath()
                                                ctx.rect(0.5, 0.5, width - 1, height - 1)
                                                ctx.stroke()
                                            }
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            text: Lang.t("cnt.tip")
                                            font.pixelSize: Theme.px(10)
                                            font.family: Theme.fontFamily
                                            color: fileDrop.containsDrag ? Theme.accentText : Theme.textDim
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (Store.tags.length === 0)
                                                    return
                                                var firstTag = Store.tags[0].name
                                                if (fileRow.levelRow)
                                                    Store.assignTagByPath(fileRow.modelData.path, firstTag)
                                                else
                                                    Store.assignTag(fileRow.index, firstTag)
                                            }
                                        }
                                    }

                                    // 行尾删除（层级模式不出现：那条 ✕ 是"从搜索结果里删掉一行"，对目录层级没意义）
                                    Rectangle {
                                        visible: !fileRow.levelRow
                                        width: 18
                                        height: 18
                                        radius: 5
                                        color: rowDeleteMouse.containsMouse ? Theme.hover : "transparent"
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        Layout.alignment: Qt.AlignVCenter

                                        Text {
                                            anchors.centerIn: parent
                                            text: "✕"
                                            font.pixelSize: Theme.px(10)
                                            font.family: Theme.fontFamily
                                            color: Theme.text3
                                        }

                                        MouseArea {
                                            id: rowDeleteMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: Store.removeFile(fileRow.index)
                                        }
                                    }
                                }
                            }
                        }

                        // 空态：core 还没给出文件数据时列表区域显示灰字提示（有数据时被行盖住）
                        // 放在 ListView 外面并居中：它不参与列表滚动，也不进列表的 model
                        Text {
                            anchors.centerIn: parent
                            visible: Store.viewRowCount() === 0
                            text: Store.browsePath.length > 0 ? Lang.t("browse.empty_level") : Lang.t("browse.empty_files")
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                        }
                    }
                }
            }

            // --- 标签库卡片（只读展示 + 拖拽源） ---
            Rectangle {
                id: tagCard
                // TagPill 的"所属落点容器"标记（相当于源工程里带 containerName 的 TagContainer）：
                // 标签从这张卡片里完全拖出去才会触发 exitedParent；源工程标签库不接这个信号（拖出只是弹回原位）
                readonly property bool dropContainer: true
                x: page.stackRail ? 0 : (filesCard.width + page.cardGap)
                y: page.stackRail ? (filesCard.height + page.cardGap) : 0
                width: page.stackRail ? workspace.width : page.railWidth
                height: page.railCollapsed ? page.cardHeadHeight : workspace.cardHeight
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: tagCardColumn
                    width: parent.width
                    spacing: 0

                    // 卡片头：标签库 + 只读 + 不可编辑（tag.svg）+ 折叠开关（点标题区或箭头都能折叠）
                    Item {
                        id: tagCardHead
                        width: parent.width
                        height: 42

                        // hover 底色（只圆上方两角，贴合卡片圆角）
                        Rectangle {
                            anchors.fill: parent
                            topLeftRadius: Theme.radiusCard
                            topRightRadius: Theme.radiusCard
                            color: tagHeadMouse.containsMouse ? Theme.surface2 : "transparent"
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            height: 16
                            spacing: 6

                            Text {
                                text: Lang.t("tag.library_title")
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(12)
                                font.weight: Font.Bold
                                font.family: Theme.fontFamily
                                color: Theme.text
                            }

                            Text {
                                text: Lang.t("browse.readonly")
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(10)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }
                        }

                        // 只读标记：tag.svg + 「不可编辑」（替换原来的 🔒 文字符号）
                        Row {
                            anchors.right: railToggle.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Image {
                                width: 13
                                height: 13
                                source: "qrc:/img/tag.svg"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: Lang.t("browse.not_editable")
                                font.pixelSize: Theme.px(10)
                                font.family: Theme.fontFamily
                                color: Theme.textGhost
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // 整个卡片头都可点（标题区）
                        MouseArea {
                            id: tagHeadMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.railCollapsed = !page.railCollapsed
                        }

                        // 折叠箭头：▾ 展开 / ▸ 折叠（声明在卡片头 MouseArea 之后，保证在最上层可点）
                        Rectangle {
                            id: railToggle
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            radius: 6
                            color: railToggleMouse.containsMouse ? Theme.hover : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "▾"
                                font.pixelSize: Theme.px(10)
                                font.family: Theme.fontFamily
                                color: Theme.text2
                                rotation: page.railCollapsed ? -90 : 0

                                Behavior on rotation {
                                    NumberAnimation { duration: 150 }
                                }
                            }

                            MouseArea {
                                id: railToggleMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.railCollapsed = !page.railCollapsed
                            }
                        }
                    }

                    // 折叠时连同分隔线一起收起（只留卡片头那一行）
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.line
                        visible: !page.railCollapsed
                    }

                    // 标签库列表（固定可视高度 = 卡片高 − 卡片头 42 − 分隔线 1，超出只在列表内部滚动；折叠时收起）
                    // 标签搜索框：按名字过滤下面的标签小片
                    // 高度从列表区里扣（输入框 26 + 上下各 6），卡片整体高度不变；折叠时一起收起
                    Item {
                        id: tagSearchHost
                        width: parent.width
                        height: page.railCollapsed ? 0 : 38
                        visible: !page.railCollapsed

                        TextField {
                            id: tagSearchField
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            height: 26
                            placeholderText: Lang.t("tag.search_placeholder")
                            placeholderTextColor: Theme.text3
                            color: Theme.text
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 0
                            bottomPadding: 0

                            background: Rectangle {
                                radius: 6
                                color: Theme.surface2
                                border.width: 1
                                border.color: Theme.line2
                            }

                            // 敲字即过滤：把文字抄到 page.tagQuery（不给 text 绑值，免得打字时打断绑定）
                            onTextChanged: page.tagQuery = text
                        }
                    }

                    Flickable {
                        id: tagCardBody
                        width: parent.width
                        height: page.railCollapsed ? 0 : (tagCard.height - page.cardHeadHeight - page.cardLineHeight - tagSearchHost.height)   // .tag-card-body{padding:8px}
                        visible: !page.railCollapsed
                        contentWidth: width
                        contentHeight: tagBodyColumn.height + 16
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        // 细竖向滚动条（按需出现）
                        ScrollBar.vertical: ScrollBar {
                            id: tagBodyBar
                            policy: ScrollBar.AlwaysOff
                            width: 7
                            padding: 0
                            background: Item { }

                            contentItem: Rectangle {
                                implicitWidth: 7
                                radius: 3.5
                                color: tagBodyBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                            }
                        }

                        Column {
                            id: tagBodyColumn
                            x: 8
                            y: 8
                            width: tagCardBody.width - 16
                            spacing: 8

                            // 说明文字
                            Rectangle {
                                width: parent.width
                                height: noteText.height + 14                 // .library-note{padding:7px 9px}
                                radius: 7
                                color: Theme.surfaceInset

                                Text {
                                    id: noteText
                                    x: 9
                                    y: 7
                                    width: parent.width - 18
                                    text: Lang.t("browse.rail_note")
                                    wrapMode: Text.WordWrap
                                    lineHeight: 1.5
                                    font.pixelSize: Theme.px(10)
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                }
                            }

                            // 空态：core 还没给出标签库数据时的灰字提示
                            // （在 Column 里用固定高度占位，不用 anchors / x / y，免得和 Column 的布局打架）
                            Text {
                                width: parent.width
                                height: 60
                                visible: Store.types.length === 0
                                text: Lang.t("tag.library_empty_hint")
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(11)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }

                            // 搜索词一个标签都没匹配上：灰字居中提示（此时类型卡片全被隐藏）
                            Text {
                                width: parent.width
                                height: 60
                                visible: page.tagSearchEmpty && Store.types.length > 0
                                text: Lang.t("tag.search_empty")
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(11)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }

                            // 按类型分组展示标签（每个 pill 都是拖拽源）
                            Column {
                                id: tagDisplayColumn
                                width: parent.width
                                spacing: 7

                                Repeater {
                                    model: Store.types

                                    delegate: Rectangle {
                                        id: typeCard

                                        required property var modelData

                                        // 这个类型下匹配搜索词的标签（搜索为空 = 全部）；过滤时一个都没匹配上就整张卡不显示
                                        readonly property var matchedTags: page.tagsMatchingQuery(typeCard.modelData.typeId)

                                        width: tagDisplayColumn.width
                                        height: typeCardColumn.height + 16       // .display-group-card{padding:8px 9px}
                                        visible: !page.tagSearchActive || typeCard.matchedTags.length > 0

                                        radius: 8
                                        color: Theme.surface
                                        border.width: 1
                                        border.color: Theme.line

                                        Column {
                                            id: typeCardColumn
                                            x: 9
                                            y: 8
                                            width: parent.width - 18
                                            spacing: 7

                                            // 色块 + 类型名 + 标签数
                                            Item {
                                                width: parent.width
                                                height: 14

                                                Rectangle {
                                                    id: typeColorBlock
                                                    anchors.left: parent.left
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: 11
                                                    height: 11
                                                    radius: 3
                                                    color: typeCard.modelData.color
                                                }

                                                Text {
                                                    anchors.left: typeColorBlock.right
                                                    anchors.leftMargin: 6
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: typeCard.modelData.name
                                                    font.pixelSize: Theme.px(11)
                                                    font.weight: Font.DemiBold
                                                    font.family: Theme.fontFamily
                                                    color: Theme.text
                                                }

                                                Text {
                                                    anchors.right: parent.right
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    // 过滤中显示命中的条数，和下面真正画出来的小片对得上
                                                    text: typeCard.matchedTags.length
                                                    font.pixelSize: Theme.px(10)
                                                    font.family: Theme.fontFamily
                                                    color: Theme.text3
                                                }
                                            }

                                            Flow {
                                                width: parent.width
                                                spacing: 5

                                                Repeater {
                                                    // 只画匹配搜索词的标签，其余不建小片（拖拽手势 / mimeData 完全不变）
                                                    model: typeCard.matchedTags

                                                    // 外面套一层 Item：拖动时只改 pill 自己的 x/y，不破坏 Flow 布局
                                                    delegate: Item {
                                                        id: pillSlot

                                                        required property var modelData

                                                        width: tagPill.width
                                                        height: tagPill.height

                                                        // 拖拽赋值的实现整个在 TagPill 里（照搬源工程 TagRectangle：
                                                        // 手动跟手移动 + 临时改挂到窗口顶层 + 松手 Drag.drop()）
                                                        TagDragPill {
                                                            id: tagPill

                                                            tagText: pillSlot.modelData.name
                                                            tagColor: typeCard.modelData.color
                                                            onPlusClicked: Store.addFilter("include", pillSlot.modelData.name)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------- 底部提示 ----------------
        Row {
            id: footerHint
            width: pageColumn.width
            height: 18
            spacing: 6

            Rectangle {
                id: dragKbd
                width: Math.max(20, dragKbdText.implicitWidth + 10)      // .kbd{min-width:20px;padding:0 5px}
                height: 18
                radius: 4
                color: Theme.surface
                border.width: 1
                border.color: Theme.line2

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width - 2
                    x: 1
                    height: 1
                    color: Theme.line2
                }

                Text {
                    id: dragKbdText
                    anchors.centerIn: parent
                    text: Lang.t("browse.kbd_drag")
                    font.pixelSize: Theme.px(10)
                    font.family: Theme.fontFamily
                    color: Theme.textMid
                }
            }

            Text {
                text: Lang.t("browse.footer_drag")
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Theme.px(10)
                font.family: Theme.fontFamily
                color: Theme.text3
            }
        }
    }

    // （拖拽时不再需要"跟随阴影"：TagPill 按下就把自己临时改挂到窗口顶层并提 z，
    //   拖出标签栏不会被 clip 裁掉，与源工程 TagRectangle 一致）

    // ---------------- 层级模式右键菜单（整页只有这一份；目标记在 page.menuPath / menuIsDir）----------------
    Menu {
        id: levelMenu

        padding: 5

        background: Rectangle {
            implicitWidth: 180
            implicitHeight: levelMenu.contentHeight + levelMenu.topPadding + levelMenu.bottomPadding
            radius: 8
            color: Theme.surface
            border.width: 1
            border.color: Theme.line2
        }

        MenuItem {
            text: page.menuIsDir ? Lang.t("browse.enter_dir") : Lang.t("browse.open_system")
            font.pixelSize: Theme.px(11)
            font.family: Theme.fontFamily

            onTriggered: {
                if (page.menuIsDir)
                    Store.enterDir(page.menuPath)
                else
                    Store.openEntryInSystem(page.menuPath)
            }
        }

        MenuItem {
            text: Lang.t("browse.reveal_system")
            font.pixelSize: Theme.px(11)
            font.family: Theme.fontFamily

            onTriggered: Store.revealEntryInSystem(page.menuPath)
        }
    }
}
