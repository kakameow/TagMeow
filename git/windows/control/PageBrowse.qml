import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 文件页面

Item {
    id: page

    // 断点与公共尺寸
    readonly property bool stackFilter: width < 1180
    readonly property bool stackRail: width < 980
    readonly property bool compact: width < 760
    readonly property bool showFileMeta: width >= 1180      // 文件行的大小 / 时间列
    readonly property bool showDropTarget: width >= 760
    readonly property int railWidth: stackFilter ? Theme.railNarrow : Theme.rail
    readonly property int cardGap: 10
    readonly property int colGap: 10
    readonly property int actionsHeight: 72
    readonly property int dirOptionHeight: 28

    // 固定高度分配
    readonly property int fixedHeight: 740                  // 窗口 820 − 顶栏 52 − 上下留白 28
    readonly property int contentSpacing: 12                // 页内各块之间的间距（pageColumn.spacing）
    readonly property int cardHeadHeight: 42                // 卡片头高度（文件 / 标签库一致）
    readonly property int cardLineHeight: 1                 // 卡片头下方 1px 分隔线
    readonly property int footerHeight: 18                  // 底部提示条固定高度
    // 筛选面板固定高度
    readonly property int filterPanelHeight: stackFilter ? 248 : 116
    readonly property int workspaceHeight: Math.max(240, page.height - pageHeader.height - filterPanelHeight
                                                         - footerHeight - contentSpacing * 3)

    // 文件卡片视图切换
    property bool gridView: false

    // 层级模式右键菜单的目标条目
    property string menuPath: ""
    property bool menuIsDir: false

    // 标签库卡片折叠开关
    property bool railCollapsed: false

    // 标签库搜索框的输入（按名字过滤标签小片 空串 = 不过滤）
    property string tagQuery: ""


    // 切页保留滚动位置：页面在 StackLayout 里一直活着 ListView 也就不会重建 这里什么都不用做
    // 只有「内容身份」变了才回到顶部 —— 换一批搜索结果（桥发 browseChanged） 进出目录层级
    Connections {
        target: ConfigBridge

        // 新搜索才回到顶部；滚到底加载下一页时保持当前位置（不然每喂一段就跳回开头）
        function onBrowseChanged() {
            if (ConfigBridge.browseTotal !== page.lastBrowseTotal) {
                page.lastBrowseTotal = ConfigBridge.browseTotal
                fileListHost.positionViewAtBeginning()
            }
        }
    }

    Connections {
        target: Store

        function onBrowsePathChanged() { fileListHost.positionViewAtBeginning() }
    }

    // 搜索框里真的写了东西（去掉首尾空格）才算在过滤：空串 / 只有空格 = 不做任何过滤
    readonly property bool tagSearchActive: tagQuery.trim().length > 0

    // 某个类型下的标签（标签库卡片渲染用）：标签/类型都来自 core 的标签库（ConfigBridge）
    // 写成页面函数是为了让它跟着 ConfigBridge.tags 的变化重算
    function tagsOfType(typeId) {
        var out = []
        var tags = ConfigBridge.tags
        for (var i = 0; i < tags.length; i++) {
            if (tags[i].typeId === typeId)
                out.push(tags[i])
        }
        return out
    }

    // 标签库里按名字过滤：大小写不敏感的子串匹配 一次只查一个类型下的标签
    function tagsMatchingQuery(typeId) {
        var all = page.tagsOfType(typeId)
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

    // 搜索词一个标签都没匹配上（标签库里显示「没有匹配的标签」此时类型卡片全被隐藏）
    readonly property bool tagSearchEmpty: {
        if (!tagSearchActive)
            return false
        for (var i = 0; i < ConfigBridge.types.length; i++) {
            if (tagsMatchingQuery(ConfigBridge.types[i].typeId).length > 0)
                return false
        }
        return true
    }

    // 页面刚建好时先按"没有筛选条件"查一次 让列表有个初始内容（与源工程 Store 的 onCompleted 一致）
    // 之后的每次查询都由「搜索」按钮发起（把三个标签容器 + 目录勾选的值交给桥）
    Component.onCompleted: Store.runQuery()

    // 两种模式共用一个列表
    // 层级模式（browseLevel 非空）：桥给这一层的直接子项 第一条是 "."（返回上一层）
    // 平铺模式（browseLevel 为空）：桥给搜索结果
    // 两种模式的行字段完全一样 所以下面还是同一套行渲染 只是数据源换一个
    readonly property bool levelMode: ConfigBridge.browseLevel.length > 0
    readonly property var rows: page.levelMode ? ConfigBridge.browseEntries : ConfigBridge.browseFiles
    readonly property int rowCount: page.rows.length
    // 上一次的结果总条数：用来区分"新搜索"和"滚动加载下一页"
    property int lastBrowseTotal: -1

    // 页面根节点固定填满内容区（StackLayout 用 implicitHeight 运行期用父高）不再跟内容走高
    implicitHeight: page.fixedHeight
    height: parent ? parent.height : page.fixedHeight

    Column {
        id: pageColumn
        width: page.width
        height: page.height
        spacing: page.contentSpacing   // .page-head{margin-bottom:12px}

        //  页面头
        PageHeader {
            id: pageHeader
            width: pageColumn.width
            eyebrow: Lang.t("browse.eyebrow")
            title: Lang.t("browse.files")
            desc: Lang.t("browse.desc")
            stacked: page.compact
        }

        // 筛选面板（搜索时一直可见：层级浏览在它下面 与搜索结果共用同一块列表区）
        Rectangle {
            id: filterPanel
            width: pageColumn.width
            height: page.filterPanelHeight
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
                spacing: 8

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

                // 分组 + 右列（宽屏左右排 窄屏上下排）
                Item {
                    id: filterBody
                    x: 12
                    width: parent.width - 24                // .filter-layout{padding:0 12px}
                    height: page.stackFilter ? (groupsHost.height + 12 + page.actionsHeight)
                                             : Math.max(groupsHost.height, page.actionsHeight)

                    // 左：标签筛选容器
                    Item {
                        id: groupsHost
                        x: 0
                        y: 0
                        width: parent.width
                        height: page.stackFilter ? (groupBoxHeight * 3 + 12) : page.actionsHeight

                        readonly property int groupBoxHeight: page.stackFilter ? 36 : page.actionsHeight

                        Repeater {
                            model: ["include", "exclude", "only"]
                            delegate: TagContainer {
                                id: groupBox

                                required property string modelData
                                required property int index
                                readonly property string kind: modelData

                                // 本组筛选条件
                                readonly property var items: {
                                    var list = []
                                    for (var i = 0; i < Store.filters.length; i++) {
                                        if (Store.filters[i].kind === kind)
                                            list.push({ index: i, tag: Store.filters[i].tag, color: Store.filters[i].color })
                                    }
                                    return list
                                }

                                // 宽屏横排三等分 窄屏整宽纵向堆叠
                                x: page.stackFilter ? 0 : index * ((groupsHost.width + 6) / 3)
                                y: page.stackFilter ? index * (height + 6) : 0
                                width: page.stackFilter ? groupsHost.width : (groupsHost.width - 12) / 3
                                height: groupsHost.groupBoxHeight

                                // 落点容器标记 + 头部 + 分组配色
                                containerName: groupBox.kind
                                iconSource: groupBox.kind === "include" ? "qrc:/img/funnel-plus.svg"
                                          : (groupBox.kind === "exclude" ? "qrc:/img/funnel-x.svg" : "qrc:/img/funnel.svg")
                                title: groupBox.kind === "include" ? Lang.t("cnt.include")
                                     : (groupBox.kind === "exclude" ? Lang.t("cnt.exclude") : Lang.t("cnt.only"))
                                groupColor: Theme.filterGroupBg[groupBox.kind]
                                groupLine: Theme.filterGroupLine[groupBox.kind]
                                tags: groupBox.items

                                // 拖标签进这一组 = 直接加进对应的筛选条件 拖出去 = 从本组移除
                                onTagDropped: (tag) => Store.addFilter(groupBox.kind, tag)
                                onTagRemoved: (chipIndex) => Store.removeFilter(chipIndex)
                            }
                        }
                    }

                    // 右：目录多选 + 搜索 / 清空
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

                                Image {
                                    source: "qrc:/img/folder.svg"
                                    sourceSize.width: 14
                                    sourceSize.height: 14
                                    Layout.preferredWidth: 14
                                    Layout.preferredHeight: 14
                                    Layout.alignment: Qt.AlignVCenter
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: Store.dirPathsSummary()
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

                        // 展开面板：用 Popup 定位在触发器下方 点击外部 / Esc 自动收起
                        Popup {
                            id: dirPopup
                            parent: msTrigger
                            x: 0
                            y: msTrigger.height + 4
                            width: msTrigger.width
                            padding: 5
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
                                                    // 显示完整目录路径（不是最后一段名字）
                                                    text: modelData.path
                                                    elide: Text.ElideMiddle
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
                                // 正在查就不给再点：一次搜索只触发一次（筛选容器的增删都不触发查询）
                                enabled: !ConfigBridge.searching
                                // 搜索 = 把三个标签容器 + 目录勾选的值交给桥（桥再交 core 查）
                                // 同时切回平铺搜索结果模式（层级浏览与搜索结果是同一块列表区的两个模式）
                                onClicked: {
                                    ConfigBridge.exitLevel()
                                    Store.runQuery()
                                    Store.toast(Lang.t("browse.search"))
                                }
                            }

                            Btn {
                                text: Lang.t("browse.clear")
                                font.pixelSize: Theme.px(11)
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                // 清空 = 只清标签筛选容器里的内容（目录勾选不动）
                                onClicked: {
                                    ConfigBridge.exitLevel()
                                    Store.clearFilters()
                                }
                            }
                        }
                    }
                }
            }
        }

        //  工作区：文件卡片 + 标签库卡片（层级浏览与平铺搜索结果在同一张文件卡片里切换）
        Item {
            id: workspace
            width: pageColumn.width
            height: page.workspaceHeight                   // 固定吃掉上面各块之后的剩余高度
            readonly property int cardHeight: page.stackRail ? Math.floor((height - page.cardGap) / 2) : height

            // 文件卡片
            Rectangle {
                id: filesCard
                x: 0
                y: 0
                width: page.stackRail ? workspace.width  : Math.max(220, workspace.width - page.railWidth - page.cardGap)
                height: workspace.cardHeight   // 固定高度（与标签库卡片等高）内容超出由列表内部滚动
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.line
                clip: true

                Column {
                    id: filesCardColumn
                    width: parent.width
                    spacing: 0

                    // 卡片头
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

                            // 条数：平铺模式 = 搜索结果条数（结果由桥下行 页面只数一下）
                            // 层级模式 = 当前层条数（含最上面那条 "."）卡片头本身不变
                            Text {
                                text: page.rowCount + Lang.t("common.count_suffix")
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

                            // 视图切换（只切样式 不改真实布局）
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

                    Item {
                        id: fileListArea
                        width: parent.width
                        height: filesCard.height - page.cardHeadHeight - page.cardLineHeight   // .file-list{padding:6px}

                        ListView {
                            id: fileListHost
                            x: 6
                            y: 6
                            width: fileListArea.width - 12
                            height: fileListArea.height - 12
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: page.row
                            // UI 只渲染一个范围：桥手里留着完整结果 滚到底再要下一段
                            onAtYEndChanged: {
                                if (atYEnd)
                                    ConfigBridge.loadMoreRows()
                            }
                            spacing: 1
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
                                readonly property int fixedWidth: 32 + (metaVisible ? 76 : 0)
                                                                      + (dropVisible ? 88 : 0)
                                readonly property int gapCount: 2 + (metaVisible ? 1 : 0) + (dropVisible ? 1 : 0)
                                readonly property int mainWidth: Math.max(80, width - 16 - fixedWidth - gapCount * page.colGap)

                                // 层级浏览时：目录行 / "." 行用 folder 图标
                                // 行交互换成双击进入 + 右键菜单 行尾删除不出现 —— 其余渲染与搜索结果完全一致
                                readonly property bool levelRow: page.levelMode
                                readonly property bool dirRow: modelData.kind === "dir" || modelData.kind === "parent"

                                // 参考稿 .file-icon.img/.doc/.video/.plain：底色按类型图标统一 16×16 svg
                                readonly property color iconBg: fileRow.dirRow ? Theme.tintGreen
                                                              : (modelData.iconKind === "img" ? Theme.tintBlue
                                                              : (modelData.iconKind === "doc" ? Theme.tintPurple
                                                              : (modelData.iconKind === "video" ? Theme.tintRed : Theme.surface3)))
                                // 文件行的图标：文件一律 file.svg，目录 folder.svg
                                readonly property string iconSource: fileRow.dirRow ? "qrc:/img/folder.svg"
                                                                                    : "qrc:/img/file.svg"

                                width: ListView.view.width                     // 列表已内缩 6px 行铺满列表宽度
                                height: Math.max(50, fileRowContent.height + 14)
                                radius: 8
                                color: fileDrop.containsDrag ? Theme.tintBlue
                                                             : (fileHover.containsMouse ? Theme.surface2 : Theme.surface)
                                border.width: 1
                                border.color: fileDrop.containsDrag ? Theme.dragLine
                                                                   : (fileHover.containsMouse ? Theme.lineSoft : "transparent")

                                // 行 hover（放在最底层 不挡行内的按钮）
                                MouseArea {
                                    id: fileHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }

                                // 行交互（两种模式都接 只是行为不同）：
                                //   双击：层级模式 —— "." 返回上一层 / 目录进入该目录（就地换成层级渲染）/ 文件用系统方式打开
                                //         平铺模式 —— 文件用系统方式打开
                                //   右键：目录 = 在系统文件管理器里打开该目录 文件 = 打开所在目录并选中该文件
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                                    onDoubleClicked: {
                                        var entry = fileRow.modelData

                                        if (entry.kind === "parent")
                                            ConfigBridge.goUpLevel()
                                        else if (entry.kind === "dir")
                                            ConfigBridge.enterDir(entry.path)
                                        else
                                            ConfigBridge.openEntryInSystem(entry.path)
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

                                TagDropArea {
                                    id: fileDrop
                                    anchors.fill: parent
                                    onTagDropped: (tag) => ConfigBridge.assignTagToFile(fileRow.modelData.path, tag)
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

                                    //    绿 = 拖进来的标签给这个文件加上（core addFileTag）
                                    //    红 = 拖进来的标签从这个文件去掉（core removeFileTag）
                                    Item {
                                        id: dropSlots
                                        visible: fileRow.dropVisible
                                        Layout.preferredWidth: 88
                                        Layout.preferredHeight: 26
                                        Layout.alignment: Qt.AlignVCenter

                                        Rectangle {
                                            id: addSlot
                                            anchors.left: parent.left
                                            width: 41
                                            height: parent.height
                                            radius: 6
                                            color: addDrop.containsDrag ? Theme.chipGreenBg : Theme.tintGreen
                                            border.width: addDrop.containsDrag ? 2 : 1
                                            border.color: addDrop.containsDrag ? Theme.okDot : Theme.chipGreenLine

                                            Text {
                                                anchors.centerIn: parent
                                                text: "＋"
                                                font.pixelSize: Theme.px(12)
                                                font.family: Theme.fontFamily
                                                color: Theme.chipGreenText
                                            }

                                            TagDropArea {
                                                id: addDrop
                                                anchors.fill: parent
                                                onTagDropped: (tag) => ConfigBridge.assignTagToFile(fileRow.modelData.path, tag)
                                            }
                                        }

                                        Rectangle {
                                            id: removeSlot
                                            anchors.right: parent.right
                                            width: 41
                                            height: parent.height
                                            radius: 6
                                            color: removeDrop.containsDrag ? Theme.dangerSoft : "transparent"
                                            border.width: removeDrop.containsDrag ? 2 : 1
                                            border.color: removeDrop.containsDrag ? Theme.danger : Theme.dangerLine

                                            Text {
                                                anchors.centerIn: parent
                                                text: "－"
                                                font.pixelSize: Theme.px(12)
                                                font.family: Theme.fontFamily
                                                color: Theme.danger
                                            }

                                            TagDropArea {
                                                id: removeDrop
                                                anchors.fill: parent
                                                onTagDropped: (tag) => ConfigBridge.removeTagFromFile(fileRow.modelData.path, tag)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 加载态：桥在查 core + 分批喂结果期间为 true
                        // 搜索结果由桥一批一批（TagServe 查回多少喂多少，每批 150 条）append 进
                        // ConfigBridge.browseFiles ListView 只建可视的那几行 —— 列表就是动态加载渲染出来的
                        Rectangle {
                            anchors.fill: parent
                            z: -1
                            visible: ConfigBridge.searching
                            color: Theme.surface
                            opacity: 0.72
                        }

                        Text {
                            anchors.centerIn: parent
                            z: -1
                            visible: ConfigBridge.searching
                            text: Lang.t("browse.searching")
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            color: Theme.accentText
                        }

                        // 空态：core 还没给出文件数据时列表区域显示灰字提示（有数据时被行盖住）
                        // 放在 ListView 外面并居中：它不参与列表滚动，也不进列表的 model
                        Text {
                            anchors.centerIn: parent
                            visible: page.rowCount === 0 && !ConfigBridge.searching
                            text: page.levelMode ? Lang.t("browse.empty_level") : Lang.t("browse.empty_files")
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                        }
                    }
                }
            }

            // 标签库卡片（只读展示 + 拖拽源）
            Rectangle {
                id: tagCard
                // 注意：这张卡片**不是**落点容器
                // 小片找不到落点容器就不做"是否脱离"判定 -> 从标签库拖出去松手一律弹回原位（不会消失）
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

                        // hover 底色（只圆上方两角 贴合卡片圆角）
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

                        // 折叠箭头：▾ 展开 / ▸ 折叠
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

                    // 标签库列表（固定可视高度 = 卡片高 − 卡片头 42 − 分隔线 1超出只在列表内部滚动折叠时收起）
                    // 标签搜索框：按名字过滤下面的标签小片
                    // 高度从列表区里扣（输入框 26 + 上下各 6）卡片整体高度不变折叠时一起收起
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
                                height: noteText.height + 14
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
                            Text {
                                width: parent.width
                                height: 60
                                visible: ConfigBridge.types.length === 0
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
                                visible: page.tagSearchEmpty && ConfigBridge.types.length > 0
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
                                    model: ConfigBridge.types

                                    delegate: Rectangle {
                                        id: typeCard

                                        required property var modelData

                                        // 这个类型下匹配搜索词的标签（搜索为空 = 全部）过滤时一个都没匹配上就整张卡不显示
                                        readonly property var matchedTags: page.tagsMatchingQuery(typeCard.modelData.typeId)

                                        width: tagDisplayColumn.width
                                        height: typeCardColumn.height + 16
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
                                                    // 只画匹配搜索词的标签 其余不建小片
                                                    model: typeCard.matchedTags

                                                    delegate: Item {
                                                        id: pillSlot

                                                        required property var modelData

                                                        width: tagChip.width
                                                        height: tagChip.height

                                                        // 拖拽整个在 control/TagRectangle.qml 里（源工程 TagRectangle：
                                                        // 手动跟手移动 + 临时改挂到窗口顶层 + 松手 Drag.drop()）
                                                        // 只读标签库：小片只当拖拽源 卡片上没有任何按钮
                                                        // 标签库卡片不是落点容器 -> 从卡片里拖出去松手一律弹回原位
                                                        TagRectangle {
                                                            id: tagChip

                                                            tagText: pillSlot.modelData.name
                                                            tagColor: typeCard.modelData.color
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

        // 底部提示
        Row {
            id: footerHint
            width: pageColumn.width
            height: 18
            spacing: 6

            Rectangle {
                id: dragKbd
                width: Math.max(20, dragKbdText.implicitWidth + 10)
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

    // 层级模式右键菜单
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

        // 右键只有一条：目录 = 在系统文件管理器里打开该目录
        // 文件 = 打开所在目录并高亮选中该文件（桥里走 shell 调用）
        MenuItem {
            text: Lang.t("browse.reveal_system")
            font.pixelSize: Theme.px(11)
            font.family: Theme.fontFamily

            onTriggered: ConfigBridge.revealInSystem(page.menuPath)
        }
    }
}
