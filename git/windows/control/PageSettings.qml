// 设置页：常规 / 支持与关于 / 数据与工具
// 参考稿 #page-settings（TagMeow_Win11_Redesign_refined.html 1446-1573 行）与 CSS .settings-*

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
// 版本 / 字号等设置项待接 SettingsBridge
// （旧的 Core 单例模块已随 corebridge 一并移除：本页只保留结构样式，
//   数据由桥的属性下行，用户操作由信号上行）
// 导出 / 导入标签库的系统文件选择框
import QtQuick.Dialogs

Item {
    id: page

    // ---------------- 固定尺寸（主壳窗口锁定 1280×820，内容区不再整页滚动） ----------------
    // 内容区可用高度 = 820 - 顶栏 52 - 内容区上下内边距 14*2 = 740
    readonly property int pageH: 740
    readonly property int gap: 12          // 页头与卡片网格之间的间距

    implicitHeight: pageH
    readonly property bool singleColumn: width < 980

    // 危险操作确认：页级只放一个弹窗，待执行的动作存在 pendingAction 里（本页只有「转换存储模式」一种）
    property string pendingAction: ""

    // 语言状态放在 Lang 单例里（Lang.current），主题状态放在 Theme 单例里（Theme.dark），
    // 字号缩放放在 Theme 单例里（Theme.fontScale / Theme.px）。三者的真值都在 core 的
    // ./config/config.json，由 ConfigBridge（内部持有 ConfigLoader + LanguageManager）暴露给 QML，
    // 本页不再自己存一份。
    //
    // 这里只做「配置 -> Theme.dark」方向的联动：
    //   * 启动时先按配置对齐一次（config.json 里存了夜间也要生效）
    //   * 之后配置一变（ConfigBridge 的 themeChanged / languageChanged）继续跟着对齐
    // Lang.current 本身就是 ConfigBridge.language 的绑定，不用在这里同步。
    // 反向（点设置项）由各行 onClicked 直接写 ConfigBridge（-> ConfigLoader -> saveConfig() 落盘），
    // 并立即改 Theme.dark，不等信号往返
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

    // 一行设置项：图标 + 标题/描述 + 右侧内容
    component SettingRow: Rectangle {
        id: row

        property string glyph: ""          // 兜底：没给 iconSource 时画这个文字/emoji
        property string iconSource: ""     // 可选 svg 图标（qrc:/img/xxx.svg）
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

            // 图标：有 iconSource 就画 18×18 的 svg，否则退回 glyph 文字
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

    // 卡片：白底 + 圆角 + 标题；高度由外层 ColumnLayout 的 Layout.fillHeight 决定，
    // 列高更大时卡片撑满列高，内容超出（列高不够）时在卡片内部滚动
    component SettingsCard: Rectangle {
        id: card

        property string title: ""
        default property alias content: bodyColumn.data

        width: parent ? parent.width : 0
        // 只提供「标题 + 正文自然高度」作为布局的首选高度；不能写 height: implicitHeight，
        // 否则该绑定会与 Layout.fillHeight 争夺高度，卡片就撑不满列高（两列底边不齐）
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

            // 卡片正文：高度 = 卡片高度 - 标题高度（列高更大时正文区跟着变高，内容超出时内部竖向滚动）
            Flickable {
                id: cardFlick
                width: parent.width
                height: Math.max(0, card.height - cardTitle.height)
                contentWidth: width
                contentHeight: bodyColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                // 细竖向滚动条（始终隐藏，滚轮/拖拽照常）
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
        height: page.pageH         // 固定高度：不再由内容撑高，也不在底部留大片空白
        spacing: page.gap

        PageHeader {
            id: headerBox
            width: parent.width
            eyebrow: Lang.t("settings.eyebrow")
            title: Lang.t("nav.settings")
            desc: Lang.t("settings.desc")
            stacked: page.width < 760
        }

        // 网格吃掉「页头之外的余下高度」：两列 fillHeight 后底边齐平，超出部分由卡片内部滚动
        GridLayout {
            width: parent.width
            height: page.pageH - headerBox.height - page.gap
            columns: page.singleColumn ? 1 : 2
            columnSpacing: 10
            rowSpacing: 10

            // ---------- 左列：一张合并卡片（原「常规」+「支持与关于」两段合并，分段标题保留在卡片内） ----------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true      // 与右列同高：整体上下边对齐
                Layout.alignment: Qt.AlignTop
                spacing: 10

                // 整列就这一张卡片：Layout.fillHeight 让它撑满列高，与右列卡片同顶同底
                SettingsCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: Lang.t("settings.group_general")                 // 卡片的标题即「常规」分段标题（参考稿 .settings-card .panel-title）

                    // 版本：只读条目文本，显示 core 的 ./config/config.json 里的 Version 字段（无 chevron）
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
                            // 循环切下一种语言：可选列表来自 ConfigBridge 扫 ./language 的结果，
                            // 切换交给 core 的 LanguageManager::loadLanguage（ConfigBridge.setLanguage
                            // 顺带把 DefaultLanguage 写回 config.json 落盘）。
                            // Lang.current 跟着 ConfigBridge.language 变，全界面 Lang.t(...) 绑定自动重算
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
                        // 描述跟着单例走：Theme.dark 一变，全界面颜色绑定自动重算
                        desc: Theme.dark ? Lang.t("theme.black_night") : Lang.t("theme.white_day")
                        clickable: false

                        Row {
                            spacing: 5
                            anchors.verticalCenter: parent.verticalCenter

                            // 白色 / 日间：色块永远取日间表，选中时 2px 亮边
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
                                        // 立即换色 + 写 core（config.json 的 Theme 字段，写即落盘）；两者都做，不等信号往返
                                        Theme.dark = false
                                        ConfigBridge.theme = 0
                                        Store.toast(Lang.t("settings.theme_changed_prefix") + Lang.t("theme.white_day"))
                                    }
                                }
                            }

                            // 黑色 / 夜间：色块取夜间表底色（安卓夜间锚点 #202124）
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
                                        // 立即换色 + 写 core（config.json 的 Theme 字段，写即落盘）；两者都做，不等信号往返
                                        Theme.dark = true
                                        ConfigBridge.theme = 1
                                        Store.toast(Lang.t("settings.theme_changed_prefix") + Lang.t("theme.black_night"))
                                    }
                                }
                            }
                        }
                    }

                    // 字体大小：SpinBox（from 10, to 18, stepSize 1，初值 = ConfigBridge.fontSize）
                    // 改动即写 ConfigBridge.fontSize -> 改 ConfigLoader::font_size_ + 立刻 saveConfig() 落盘
                    //（无确认按钮），并由 Theme.px(n) = Math.round(n * fontSize / 12) 驱动整个界面的字号实时重算
                    // 文案键用 Lang.qml 里实际存在的 "settings.font_size" / "settings.font_size_hint"（%1 = 基准 px）
                    SettingRow {
                        iconSource: "qrc:/img/settings-2.svg"
                        title: Lang.t("settings.font_size")
                        desc: Lang.t("settings.font_size_hint", ConfigBridge.fontSize)
                        clickable: false

                        // SpinBox：from 10 / to 18 / step 1，value 绑定 ConfigBridge.fontSize
                        // 只改外观：Basic 模板两侧指示器各 40px 宽（把内容挤没了），这里收成 24px 并套 Theme 配色
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

                            // 改即生效：用户一改就写 ConfigBridge.fontSize（-> ConfigLoader::font_size_
                            // -> saveConfig() 落盘，没有确认按钮）。
                            // 用 onValueModified 而不是 onValueChanged：只在用户真的动过控件时才写，
                            // 免得初始化时 SpinBox 为自己 10-18 的范围把配置里的越界值顺手改掉
                            //（core 认 6-48，越界它会拒绝并回弹，ConfigBridge 会发 errorOccurred 弹 Toast）
                            onValueModified: {
                                ConfigBridge.fontSize = value
                            }
                        }
                    }

                    // 卡片内的小分段标题：保留原来的「支持与关于」分段（12px/700、左内边距 14、上下 8/4）
                    // 行高由 Text 自己的 padding 撑开（padding 计入 implicitHeight），行间距与参考稿 .settings-card .panel-title 一致
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

            // ---------- 右列 ----------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true      // 两列等高（恢复）
                Layout.alignment: Qt.AlignTop
                spacing: 10

                SettingsCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true      // 与左列卡片同顶同底
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
                        // 批量改写全部受管文件：先弹确认，确认后才真的转换
                        onClicked: {
                            page.pendingAction = "convertMode"
                            confirmDialog.open()
                        }
                    }

                    SettingRow {
                        iconSource: "qrc:/img/rotate-cw.svg"
                        title: Lang.t("settings.refresh_index")
                        desc: Lang.t("settings.refresh_desc")
                        showChevron: true
                        onClicked: ConfigBridge.refreshIndex()
                    }

                    SettingRow {
                        iconSource: "qrc:/img/trash.svg"
                        title: Lang.t("settings.cleanup")
                        desc: Lang.t("settings.cleanup_desc")
                        showChevron: true
                        onClicked: ConfigBridge.clearInvalidData()
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

    // ---------- 帮助 / 关于：信息弹窗（原来只弹一个写着功能名的占位 toast） ----------
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

    // （「关于 TagMeow」不再弹说明窗：整行直接调系统浏览器打开仓库）

    // ---------- 危险操作确认弹窗（本页只有「转换存储模式」需要确认）----------
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
            text: page.pendingAction === "convertMode" ? Lang.t("confirm.convert_mode") : ""
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            color: Theme.text2
        }

        footer: Item {
            implicitHeight: 44

            // 取消：清掉待执行动作，什么都不做
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

            // 确认：先取出并清空待执行动作，再真正转换
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
                    if (action === "convertMode")
                        ConfigBridge.convertMode()
                }
            }
        }
    }

    // ---------- 数据与工具：导出 / 导入标签库（系统文件框 -> core 读写 tag.json） ----------
    FileDialog {
        id: exportDialog

        title: Lang.t("btn.export")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["TagMeow JSON (*.json)"]
        // selectedFile 是 file:// URL，桥里用 QUrl::toLocalFile 转成本地路径
        onAccepted: ConfigBridge.exportLibrary(selectedFile.toString())
    }

    FileDialog {
        id: importDialog

        title: Lang.t("btn.import")
        fileMode: FileDialog.OpenFile
        nameFilters: ["TagMeow JSON (*.json)"]
        onAccepted: ConfigBridge.importLibrary(selectedFile.toString())
    }
}
