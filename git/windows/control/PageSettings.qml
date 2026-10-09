import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// 设置页面

Item {
    id: page

    // 固定尺寸
    // 内容区可用高度 = 820 - 顶栏 52 - 内容区上下内边距 14*2 = 740
    readonly property int pageH: 740
    readonly property int gap: 12          // 页头与卡片网格之间的间距

    implicitHeight: pageH
    readonly property bool singleColumn: width < 980

    // 危险操作确认：页级只放一个弹窗 待执行的动作存在 pendingAction 里（本页只有「转换存储模式」一种）
    property string pendingAction: ""

    // 语言状态放在 Lang 单例里（Lang.current） 主题状态放在 Theme 单例里（Theme.dark）
    // 字号缩放放在 Theme 单例里（Theme.fontScale / Theme.px） 三者的真值都在 core 的
    // ./config/config.json，由 ConfigBridge（内部持有 ConfigLoader + LanguageManager）暴露给 QML
    // 本页不再自己存一份

    // 这里只做「配置 -> Theme.dark」方向的联动：
    //   * 启动时先按配置对齐一次（config.json 里存了夜间也要生效）
    //   * 之后配置一变（ConfigBridge 的 themeChanged / languageChanged）继续跟着对齐
    // Lang.current 本身就是 ConfigBridge.language 的绑定 不用在这里同步
    // 反向（点设置项）由各行 onClicked 直接写 ConfigBridge（-> ConfigLoader -> saveConfig() 落盘）
    // 并立即改 Theme.dark 不等信号往返
    Component.onCompleted: applyConfigToSingletons()

    Connections {
        target: ConfigBridge

        function onThemeChanged() {
            page.applyConfigToSingletons()
        }

        function onLanguageChanged() {
            page.applyConfigToSingletons()
        }
    }

    function applyConfigToSingletons() {
        Theme.dark = (ConfigBridge.theme === 1)     // 0 = 白 / 日间，1 = 黑 / 夜间
    }

    // 数据与工具：确认 + 结果提示
    // 危险 / 批量操作都先过确认弹窗确认后执行 再弹一个"成功与否"的结果弹窗
    // （成功显示桥从 core 读回来的数据回报 lastReport 失败显示 lastError）
    property string pendingFile: ""

    function askConfirm(action) {
        page.pendingAction = action
        confirmDialog.open()
    }

    // 每个动作在界面上叫什么（结果弹窗标题用）
    function actionLabel(action) {
        if (action === "convertMode")
            return Lang.t("settings.mode")
        if (action === "refreshIndex")
            return Lang.t("settings.refresh_index")
        if (action === "cleanup")
            return Lang.t("settings.cleanup")
        if (action === "export")
            return Lang.t("btn.export")
        if (action === "import")
            return Lang.t("btn.import")
        return action
    }

    // 真正执行 + 弹结果（export / import 的路径放在 pendingFile 里）
    function runTool(action) {
        var ok = false

        if (action === "convertMode")
            ok = ConfigBridge.convertMode()
        else if (action === "refreshIndex")
            ok = ConfigBridge.refreshIndex()
        else if (action === "cleanup")
            ok = ConfigBridge.clearInvalidData()
        else if (action === "export")
            ok = ConfigBridge.exportLibrary(page.pendingFile)
        else if (action === "import")
            ok = ConfigBridge.importLibrary(page.pendingFile)

        // 先弹提示
        var label = page.actionLabel(action)
        var detail = ok ? ConfigBridge.lastReport : ConfigBridge.lastError()
        var tail = detail.length > 0 ? " · " + detail : ""
        Store.toast(Lang.t(ok ? "tool.done" : "tool.failed") + " · " + label + tail)

        return ok
    }

    // 一行设置项：图标 + 标题/描述 + 右侧内容
    component SettingRow: Rectangle {
        id: row

        property string glyph: ""
        property string iconSource: ""
        property string title: ""
        property string desc: ""
        property bool clickable: true
        property bool showChevron: false
        property string valueText: ""
        property string badgeText: ""
        default property alias trailing: trailingRow.data   // 子项进右侧行

        width: parent ? parent.width : 0
        height: 50
        color: rowMouse.containsMouse && row.clickable ? Theme.surface2 : "transparent"

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: row.clickable
            cursorShape: row.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (row.clickable)
                    row.clicked()
            }
        }

        signal clicked()

        Rectangle {
            id: iconBox
            width: 30
            height: 30
            radius: 8
            color: Theme.surfaceInsetHover
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter

            // 图标
            Image {
                anchors.centerIn: parent
                visible: row.iconSource.length > 0
                width: 18
                height: 18
                source: row.iconSource
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Text {
                anchors.centerIn: parent
                visible: row.iconSource.length === 0
                text: row.glyph
                font.pixelSize: Theme.px(15)
                font.family: Theme.fontFamily
            }
        }

        Column {
            anchors.left: iconBox.right
            anchors.leftMargin: 10
            anchors.right: trailingRow.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                text: row.title
                font.pixelSize: Theme.px(12)
                font.weight: Font.DemiBold
                font.family: Theme.fontFamily
                color: Theme.text
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: row.desc.length > 0
                text: row.desc
                font.pixelSize: Theme.px(10)
                font.family: Theme.fontFamily
                color: Theme.text3
                elide: Text.ElideRight
                width: parent.width
            }
        }

        Row {
            id: trailingRow
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                visible: row.valueText.length > 0
                text: row.valueText
                font.pixelSize: Theme.px(12)
                font.family: Theme.fontFamily
                color: Theme.text2
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                visible: row.showChevron
                text: "›"
                font.pixelSize: Theme.px(14)
                color: Theme.iconFaint
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                visible: row.badgeText.length > 0
                width: badgeText.implicitWidth + 12
                height: 18
                radius: 5
                color: Theme.surfaceInsetHover
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: row.badgeText
                    font.pixelSize: Theme.px(10)
                    font.family: Theme.fontFamily
                    color: Theme.text3
                }
            }
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.lineSoft
            visible: row.parent !== null && row.parent.children.length > 1
        }
    }

    // 卡片：白底 + 圆角 + 标题
    component SettingsCard: Rectangle {
        id: card

        property string title: ""
        default property alias content: bodyColumn.data

        width: parent ? parent.width : 0
        implicitHeight: cardTitle.height + bodyColumn.implicitHeight
        radius: 10
        color: Theme.surface
        border.width: 1
        border.color: Theme.line

        Column {
            id: cardColumn
            width: parent.width

            Text {
                id: cardTitle
                text: card.title
                font.pixelSize: Theme.px(12)
                font.weight: Font.Bold
                font.family: Theme.fontFamily
                color: Theme.text2
                anchors.left: parent.left
                anchors.leftMargin: 14
                topPadding: 12
                bottomPadding: 8
            }

            // 卡片正文
            Flickable {
                id: cardFlick
                width: parent.width
                height: Math.max(0, card.height - cardTitle.height)
                contentWidth: width
                contentHeight: bodyColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                // 细竖向滚动条
                ScrollBar.vertical: ScrollBar {
                    id: cardBar
                    policy: ScrollBar.AlwaysOff
                    width: 7
                    padding: 0
                    background: Item { }

                    contentItem: Rectangle {
                        implicitWidth: 7
                        radius: 3.5
                        color: cardBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                    }
                }

                Column {
                    id: bodyColumn
                    width: cardFlick.width
                }
            }
        }
    }

    Column {
        id: mainColumn
        width: parent.width
        height: page.pageH
        spacing: page.gap

        PageHeader {
            id: headerBox
            width: parent.width
            eyebrow: Lang.t("settings.eyebrow")
            title: Lang.t("nav.settings")
            desc: Lang.t("settings.desc")
            stacked: page.width < 760
        }

        GridLayout {
            width: parent.width
            height: page.pageH - headerBox.height - page.gap
            columns: page.singleColumn ? 1 : 2
            columnSpacing: 10
            rowSpacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                spacing: 10

                SettingsCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: Lang.t("settings.group_general")

                    SettingRow {
                        iconSource: "qrc:/img/database-plus.svg"
                        title: Lang.t("settings.version")
                        desc: Lang.t("settings.version_hint")
                        valueText: ConfigBridge.version
                        clickable: false
                    }

                    SettingRow {
                        iconSource: "qrc:/img/settings-2.svg"
                        title: Lang.t("settings.language")
                        desc: Lang.t("settings.language_hint").trim()
                        valueText: Lang.current
                        showChevron: true
                        onClicked: {
                            // 循环切下一种语言：可选列表来自 ConfigBridge 扫 ./language 的结果
                            // 切换交给 core 的 LanguageManager::loadLanguage（ConfigBridge.setLanguage
                            // 顺带把 DefaultLanguage 写回 config.json 落盘）
                            // Lang.current 跟着 ConfigBridge.language 变 全界面 Lang.t(...) 绑定自动重算
                            var list = Lang.languages
                            if (list.length === 0)
                                return
                            var i = list.indexOf(Lang.current)
                            var next = list[(i + 1) % list.length]
                            ConfigBridge.setLanguage(next)
                            Store.toast(Lang.t("settings.language_saved_prefix") + Lang.current)
                        }
                    }

                    SettingRow {
                        iconSource: "qrc:/img/paint-roller.svg"
                        title: Lang.t("settings.theme")
                        // 描述跟着单例走：Theme.dark 一变 全界面颜色绑定自动重算
                        desc: Theme.dark ? Lang.t("theme.black_night") : Lang.t("theme.white_day")
                        clickable: false

                        Row {
                            spacing: 5
                            anchors.verticalCenter: parent.verticalCenter

                            // 白色 / 日间：色块永远取日间表 选中时 2px 亮边
                            Rectangle {
                                width: 26
                                height: 26
                                radius: 6
                                color: Theme.day.surface
                                border.width: !Theme.dark ? 2 : 1
                                border.color: !Theme.dark ? Theme.accent : Theme.line2

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // 立即换色 + 写 core（config.json 的 Theme 字段 写即落盘）两者都做 不等信号往返
                                        Theme.dark = false
                                        ConfigBridge.theme = 0
                                        Store.toast(Lang.t("settings.theme_changed_prefix") + Lang.t("theme.white_day"))
                                    }
                                }
                            }

                            // 黑色 / 夜间：色块取夜间表底色
                            Rectangle {
                                width: 26
                                height: 26
                                radius: 6
                                color: Theme.night.bg
                                border.width: Theme.dark ? 2 : 0
                                border.color: Theme.accent

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // 立即换色 + 写 core（config.json 的 Theme 字段 写即落盘）两者都做 不等信号往返
                                        Theme.dark = true
                                        ConfigBridge.theme = 1
                                        Store.toast(Lang.t("settings.theme_changed_prefix") + Lang.t("theme.black_night"))
                                    }
                                }
                            }
                        }
                    }

                    // 字体大小：SpinBox（from 10 to 18 stepSize 1 初值 = ConfigBridge.fontSize）
                    // 改动即写 ConfigBridge.fontSize -> 改 ConfigLoader::font_size_ + 立刻 saveConfig() 落盘
                    //（无确认按钮）并由 Theme.px(n) = Math.round(n * fontSize / 12) 驱动整个界面的字号实时重算
                    // 文案键用 Lang.qml 里实际存在的 "settings.font_size" / "settings.font_size_hint"（%1 = 基准 px）
                    SettingRow {
                        iconSource: "qrc:/img/settings-2.svg"
                        title: Lang.t("settings.font_size")
                        desc: Lang.t("settings.font_size_hint", ConfigBridge.fontSize)
                        clickable: false

                        SpinBox {
                            id: fontSizeSpin
                            from: 10
                            to: 18
                            stepSize: 1
                            value: ConfigBridge.fontSize
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: 116
                            implicitHeight: 28
                            font.pixelSize: Theme.px(12)
                            font.family: Theme.fontFamily
                            palette.text: Theme.text

                            background: Rectangle {
                                radius: 6
                                color: Theme.surface2
                                border.width: 1
                                border.color: Theme.line2
                            }

                            // 右侧：＋（按下变底色）
                            up.indicator: Rectangle {
                                x: fontSizeSpin.width - width
                                height: fontSizeSpin.height
                                implicitWidth: 24
                                radius: 6
                                color: fontSizeSpin.up.pressed ? Theme.surfaceInset : "transparent"

                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    font.pixelSize: Theme.px(11)
                                    font.family: Theme.fontFamily
                                    color: fontSizeSpin.enabled ? Theme.text2 : Theme.textGhost
                                }
                            }

                            // 左侧：−（按下变底色）
                            down.indicator: Rectangle {
                                x: 0
                                height: fontSizeSpin.height
                                implicitWidth: 24
                                radius: 6
                                color: fontSizeSpin.down.pressed ? Theme.surfaceInset : "transparent"

                                Text {
                                    anchors.centerIn: parent
                                    text: "−"
                                    font.pixelSize: Theme.px(11)
                                    font.family: Theme.fontFamily
                                    color: fontSizeSpin.enabled ? Theme.text2 : Theme.textGhost
                                }
                            }

                            // 改即生效：用户一改就写 ConfigBridge.fontSize（-> ConfigLoader::font_size_ -> saveConfig() 落盘 没有确认按钮）
                            onValueModified: {
                                ConfigBridge.fontSize = value
                            }
                        }
                    }

                    // 卡片内的小分段标题
                    Text {
                        width: parent ? parent.width : 0
                        text: Lang.t("settings.group_support")
                        leftPadding: 14
                        topPadding: 8
                        bottomPadding: 4
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text2
                        elide: Text.ElideRight
                    }

                    SettingRow {
                        iconSource: "qrc:/img/circle-question-mark.svg"
                        title: Lang.t("bar.help")
                        desc: Lang.t("settings.help_desc")
                        showChevron: true
                        onClicked: helpDialog.open()
                    }

                    SettingRow {
                        iconSource: "qrc:/img/tagmeow.svg"
                        title: Lang.t("settings.about")
                        desc: Lang.t("settings.about_desc")
                        showChevron: true
                        onClicked: Qt.openUrlExternally("https://github.com/kakameow/TagMeow")   // 直接跳系统浏览器，不弹窗
                    }

                    SettingRow {
                        iconSource: "qrc:/img/settings-2.svg"
                        title: Lang.t("settings.advanced")
                        desc: Lang.t("settings.advanced_desc")
                        badgeText: Lang.t("common.reserved")
                    }
                }
            }

            // 右列
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignTop
                spacing: 10

                SettingsCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: Lang.t("settings.group_data")

                    SettingRow {
                        iconSource: "qrc:/img/database-plus.svg"
                        title: Lang.t("settings.status")
                        desc: Lang.t("settings.status_line", ConfigBridge.dirCount, ConfigBridge.fileCount, ConfigBridge.tagMode)
                        clickable: false
                    }

                    SettingRow {
                        iconSource: "qrc:/img/rotate-cw.svg"
                        title: Lang.t("settings.mode")
                        desc: Lang.t("settings.mode_current_prefix") + ConfigBridge.tagMode + Lang.t("settings.mode_hint")
                        valueText: ConfigBridge.tagMode
                        showChevron: true
                        // 批量改写全部受管文件：先弹确认 确认后才真的转换
                        onClicked: page.askConfirm("convertMode")
                    }

                    SettingRow {
                        iconSource: "qrc:/img/rotate-cw.svg"
                        title: Lang.t("settings.refresh_index")
                        desc: Lang.t("settings.refresh_desc")
                        showChevron: true
                        // 整体重扫会重建索引 先确认 确认后弹"成功与否"结果
                        onClicked: page.askConfirm("refreshIndex")
                    }

                    SettingRow {
                        iconSource: "qrc:/img/trash.svg"
                        title: Lang.t("settings.cleanup")
                        desc: Lang.t("settings.cleanup_desc")
                        showChevron: true
                        // 删数据库记录也是破坏性操作 同样先确认再执行
                        onClicked: page.askConfirm("cleanup")
                    }

                    SettingRow {
                        iconSource: "qrc:/img/download.svg"
                        title: Lang.t("btn.export")
                        desc: Lang.t("settings.export_desc")
                        showChevron: true
                        onClicked: exportDialog.open()
                    }

                    SettingRow {
                        iconSource: "qrc:/img/download.svg"
                        title: Lang.t("btn.import")
                        desc: Lang.t("settings.import_desc")
                        showChevron: true
                        onClicked: importDialog.open()
                    }
                }
            }
        }
    }

    //  帮助 / 关于：信息弹窗
    Dialog {
        id: helpDialog
        anchors.centerIn: parent
        width: Math.min(520, page.width - 40)
        modal: true
        title: Lang.t("help.title")

        background: Rectangle {
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
        }

        contentItem: Text {
            text: Lang.t("help.body")
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            color: Theme.text2
        }

        footer: Item {
            implicitHeight: 44

            Btn {
                text: Lang.t("common.know")
                kind: "primary"
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: helpDialog.close()
            }
        }
    }

    // 危险操作确认弹窗
    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        width: Math.min(420, page.width - 40)
        modal: true
        title: Lang.t("confirm.title")

        background: Rectangle {
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
        }

        contentItem: Text {
            text: page.pendingAction === "convertMode" ? Lang.t("confirm.convert_mode")
                : (page.pendingAction === "refreshIndex" ? Lang.t("confirm.refresh_index")
                : (page.pendingAction === "cleanup" ? Lang.t("confirm.cleanup") : ""))
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            color: Theme.text2
        }

        footer: Item {
            implicitHeight: 44

            // 取消：清掉待执行动作 什么都不做
            Btn {
                text: Lang.t("confirm.cancel")
                kind: "ghost"
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    page.pendingAction = ""
                    confirmDialog.close()
                }
            }

            // 确认：先取出并清空待执行动作 再真正转换
            Btn {
                text: Lang.t("common.ok")
                kind: "danger"
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    var action = page.pendingAction
                    page.pendingAction = ""
                    confirmDialog.close()
                    if (action.length > 0)
                        page.runTool(action)
                }
            }
        }
    }

    // --------- 数据与工具：导出 / 导入标签库（系统文件框 -> core 读写 tag.json）
    FileDialog {
        id: exportDialog

        title: Lang.t("btn.export")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["TagMeow JSON (*.json)"]
        onAccepted: {
            page.pendingFile = selectedFile.toString()
            page.runTool("export")
        }
    }

    FileDialog {
        id: importDialog

        title: Lang.t("btn.import")
        fileMode: FileDialog.OpenFile
        nameFilters: ["TagMeow JSON (*.json)"]
        onAccepted: {
            page.pendingFile = selectedFile.toString()
            page.runTool("import")
        }
    }
}