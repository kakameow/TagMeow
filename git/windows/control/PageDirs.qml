import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtCore

// 目录页面

Item {
    id: page

    readonly property bool stackRows: width < 760

    // 固定尺寸：窗口锁 1280x820 -> 内容区高 740（与其它页一致）
    readonly property int contentH: 740
    implicitHeight: page.contentH

    // 危险操作确认：页级只放一个弹窗 待执行的动作与目标行存在 pendingAction / pendingIndex 里
    property string pendingAction: ""
    property int pendingIndex: -1

    // 目录有效性由 core 判定（DirectoryConfigManager::loadFromFile() 会按磁盘真实情况重算 dir.valid）
    // 页面出现时再核一次 界面只负责渲染 dir.valid 不再有「标记失效 / 标记有效」的按钮
    Component.onCompleted: ConfigBridge.revalidateDirs()

    Column {
        id: col
        width: parent.width
        spacing: 10

        // 页头
        PageHeader {
            id: head
            width: col.width
            eyebrow: Lang.t("dir.eyebrow")
            title: Lang.t("nav.directory")
            desc: Lang.t("dir.desc")
            stacked: page.stackRows
            actionsRightMargin: 0           // 贴到最右边（与下方卡片右边缘齐平）
            actionsAlignToTitle: true       // 右上角按钮与左侧文字行（h1「目录」）垂直居中对齐

            Btn {
                text: Lang.t("btn.addDir")
                kind: "primary"
                iconSource: "qrc:/img/folder-plus.svg"
                onClicked: dirDialog.open()
            }
        }

        Rectangle {
            id: panel
            width: col.width
            clip: true
            height: Math.max(200, page.contentH - head.height - col.spacing)
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.line

            Column {
                id: panelCol
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 4   // .panel-title{margin-bottom:4px}

                Row {
                    id: panelTitle
                    spacing: 6
                    height: 17

                    Text {
                        text: Lang.t("dir.panel_title")
                        height: 17
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    Text {
                        text: ConfigBridge.dirs.length + Lang.t("common.count_suffix")
                        height: 17
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.px(10)
                        font.weight: Font.Medium
                        font.family: Theme.fontFamily
                        color: Theme.text3
                    }
                }

                // 目录行：model 绑 ConfigBridge.dirs.length，行内用 ConfigBridge.dirs[index] 取值
                //（桥在目录增删 / 刷新 / 有效性重算后发 dirsChanged 列表与文件数就实时跟着变）
                // 容器固定高度 + 内部滚动（超出才滚 滚动条隐藏）
                Flickable {
                    id: rowsFlick
                    width: parent.width
                    height: Math.max(60, panel.height - 24 - panelTitle.height - panelCol.spacing)
                    contentWidth: width
                    contentHeight: rowsColumn.height
                    clip: true
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOff
                    }

                    // 空态：core 还没给出目录数据时列表区域显示灰字提示（有数据时被行盖住）
                    Text {
                        x: Math.max(0, (rowsFlick.width - width) / 2)
                        y: Math.max(0, (rowsFlick.height - height) / 2)
                        visible: ConfigBridge.dirs.length === 0
                        text: Lang.t("dir.empty")
                        font.pixelSize: Theme.px(11)
                        font.family: Theme.fontFamily
                        color: Theme.text3
                    }

                    Column {
                        id: rowsColumn
                        width: rowsFlick.width
                        spacing: 0

                    Repeater {
                        model: ConfigBridge.dirs.length
                        delegate: Item {
                            id: dirRow

                            required property int index
                            readonly property var dir: ConfigBridge.dirs[index]
                            readonly property bool lastRow: dirRow.index === ConfigBridge.dirs.length - 1
                            readonly property bool stacked: dirRow.width < 760
                            readonly property real padTop: 9
                            readonly property real padBottom: dirRow.lastRow ? 2 : 9
                            readonly property real pathAvail: Math.max(48, dirRow.width - leftBox.width - actionsRow.width - 24)

                            width: parent.width
                            height: dirRow.stacked
                                    ? (dirRow.padTop + leftBox.height + 6 + pathBox.height + 6 + actionsRow.height + dirRow.padBottom)
                                    : (Math.max(leftBox.height, pathBox.height, actionsRow.height) + dirRow.padTop + dirRow.padBottom)

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onDoubleClicked: {
                                    if (ConfigBridge.openDir(dirRow.index))
                                        Store.goTo("browse")
                                }
                            }

                            Rectangle {
                                visible: !dirRow.lastRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.lineSoft
                            }

                            // 左：状态点 + 名称 / 文件数
                            Row {
                                id: leftBox
                                spacing: 8
                                anchors.left: parent.left
                                anchors.top: dirRow.stacked ? parent.top : undefined
                                anchors.topMargin: dirRow.stacked ? dirRow.padTop : 0
                                anchors.verticalCenter: dirRow.stacked ? undefined : parent.verticalCenter

                                Item {
                                    id: dot
                                    width: 13
                                    height: 13
                                    anchors.verticalCenter: parent.verticalCenter

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6.5
                                        color: dirRow.dir.valid ? Theme.tintGreen : Theme.tintGray
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 7
                                        height: 7
                                        radius: 3.5
                                        color: dirRow.dir.valid ? Theme.okDot : Theme.iconFaint
                                    }
                                }

                                Column {
                                    id: metaCol
                                    spacing: 2   // .row-desc{margin-top:2px}
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        text: dirRow.dir.name
                                        height: 17
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(12)
                                        font.weight: Font.DemiBold
                                        font.family: Theme.fontFamily
                                        color: Theme.text
                                    }

                                    Text {
                                        text: Lang.t("dir.contains_files", dirRow.dir.count)
                                        height: 15
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.family: Theme.fontFamily
                                        color: Theme.text3
                                    }
                                }
                            }

                            // 中间：路径
                            Rectangle {
                                id: pathBox
                                height: 23
                                radius: 6
                                color: Theme.surfaceInset

                                anchors.left: dirRow.stacked ? parent.left : leftBox.right
                                anchors.leftMargin: dirRow.stacked ? 0 : 12
                                anchors.top: dirRow.stacked ? leftBox.bottom : undefined
                                anchors.topMargin: dirRow.stacked ? 6 : 0
                                anchors.verticalCenter: dirRow.stacked ? undefined : parent.verticalCenter
                                width: dirRow.stacked
                                       ? dirRow.width
                                       : Math.max(48, Math.min(200, Math.min(pathText.implicitWidth + 16, dirRow.pathAvail)))

                                Text {
                                    id: pathText
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 16
                                    text: dirRow.dir.path
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.px(10)
                                    font.family: Theme.fontMono
                                    color: dirRow.dir.valid ? Theme.textPath : Theme.text3
                                    font.strikeout: !dirRow.dir.valid
                                }
                            }

                            // 右：打开 / 刷新 / 移除
                            Row {
                                id: actionsRow
                                spacing: 5
                                anchors.right: parent.right
                                anchors.top: dirRow.stacked ? pathBox.bottom : undefined
                                anchors.topMargin: dirRow.stacked ? 6 : 0
                                anchors.verticalCenter: dirRow.stacked ? undefined : parent.verticalCenter

                                Btn {
                                    text: Lang.t("dir.open")
                                    small: true
                                    onClicked: {
                                        if (ConfigBridge.openDir(dirRow.index))
                                            Store.goTo("browse")
                                    }
                                }

                                // 刷新：单目录重扫会重建这个目录的记录
                                Btn {
                                    text: Lang.t("common.refresh")
                                    small: true
                                    onClicked: {
                                        page.pendingAction = "refreshDir"
                                        page.pendingIndex = dirRow.index
                                        confirmDialog.open()
                                    }
                                }

                                Btn {
                                    text: Lang.t("dir.remove")
                                    kind: "danger"
                                    small: true
                                    onClicked: {
                                        page.pendingAction = "removeDir"
                                        page.pendingIndex = dirRow.index
                                        confirmDialog.open()
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

    // 系统目录选择框
    // 与同步页「选择目录」/ 设置页导入导出用的是同一套系统选择器
    // 选中 -> 把 file:// URL 原样交给桥（桥负责转成本地路径 + 校验 + 建索引） 取消 -> 什么都不做
    FolderDialog {
        id: dirDialog

        title: Lang.t("dir.browser_title")
        // 标准路径（file:/// 形式）：已有索引目录就从第一个开始 否则从系统主目录开始
        currentFolder: ConfigBridge.dirs.length > 0
                       ? "file:///" + String(ConfigBridge.dirs[0].path).replace(/\\/g, "/")
                       : StandardPaths.writableLocation(StandardPaths.HomeLocation)

        onAccepted: ConfigBridge.addDir(selectedFolder.toString())
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
            text: page.pendingAction === "removeDir"
                  ? Lang.t("confirm.remove_dir")
                  : (page.pendingAction === "refreshDir" ? Lang.t("confirm.refresh_dir") : "")
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
                    page.pendingIndex = -1
                    confirmDialog.close()
                }
            }

            // 确认：先取出并清空待执行动作 再真正执行（两个动作都走桥 桥里再碰 core）
            Btn {
                text: Lang.t("common.ok")
                kind: "danger"
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    var action = page.pendingAction
                    var index = page.pendingIndex
                    page.pendingAction = ""
                    page.pendingIndex = -1
                    confirmDialog.close()
                    if (index < 0)
                        return
                    if (action === "removeDir")
                        ConfigBridge.removeDir(index)
                    else if (action === "refreshDir")
                        ConfigBridge.refreshDir(index)
                }
            }
        }
    }
}
