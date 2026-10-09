import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "control"

ApplicationWindow {
    id: window

    width: 1280
    height: 820
    minimumWidth: 320
    minimumHeight: 420
    visible: true
    title: Lang.t("app.name")
    color: Theme.bg

    readonly property bool narrow: width < 1180
    readonly property bool compact: width < 760
    readonly property int sideWidth: narrow ? Theme.sidebarNarrow : Theme.sidebar
    readonly property int pad: Theme.padFor(width)

    // 顶栏
    Rectangle {
        id: topbar
        width: parent.width
        height: Theme.topH
        color: Theme.surface
        z: 20

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.line
        }

        Row {
            id: brand
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: window.compact ? undefined : window.sideWidth
            spacing: 8
            leftPadding: 12

            Rectangle {
                width: 26
                height: 26
                radius: 7
                anchors.verticalCenter: parent.verticalCenter
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#6a81ff" }
                    GradientStop { position: 1.0; color: "#4263f5" }
                }

                Text {
                    anchors.centerIn: parent
                    text: "T"
                    color: "#ffffff"
                    font.pixelSize: Theme.px(12)
                    font.weight: Font.ExtraBold
                    font.family: Theme.fontFamily
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    text: Lang.t("app.name")
                    font.pixelSize: Theme.px(12)
                    font.weight: Font.Bold
                    font.family: Theme.fontFamily
                    color: Theme.text
                }

                Text {
                    visible: !window.compact
                    text: Lang.t("app.tagline")
                    font.pixelSize: Theme.px(10)
                    font.family: Theme.fontFamily
                    color: Theme.text3
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: [
                    { label: Lang.t("stat.files_prefix"), value: ConfigBridge.fileCount },
                    { label: Lang.t("stat.tags_prefix"), value: ConfigBridge.tags.length }
                ]

                delegate: Rectangle {
                    required property var modelData
                    height: 28
                    width: statRow.implicitWidth + 20
                    radius: 7
                    color: statMouse.containsMouse ? Theme.surfaceInsetHover : Theme.surface3

                    Row {
                        id: statRow
                        anchors.centerIn: parent
                        spacing: 5

                        Text {
                            text: modelData.label
                            font.pixelSize: Theme.px(12)
                            font.family: Theme.fontFamily
                            color: Theme.text3
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: modelData.value
                            font.pixelSize: Theme.px(12)
                            font.weight: Font.Bold
                            font.family: Theme.fontFamily
                            color: Theme.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: statMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
            }
        }
    }

    // 侧栏
    Rectangle {
        id: sidebar
        visible: !window.compact
        width: window.sideWidth
        anchors.top: topbar.bottom
        anchors.bottom: parent.bottom
        color: Theme.surface2
        z: 10

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.line
        }

        Column {
            id: navColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 8
            spacing: 1

            NavItem {
                width: navColumn.width
                glyph: "▦"
                iconSource: "qrc:/img/file.svg"
                text: Lang.t("browse.files")
                count: ConfigBridge.fileCount
                active: Store.currentPage === "browse"
                onClicked: Store.currentPage = "browse"
            }

            NavItem {
                width: navColumn.width
                glyph: "▱"
                iconSource: "qrc:/img/folder.svg"
                text: Lang.t("nav.directory")
                count: ConfigBridge.dirs.length
                active: Store.currentPage === "dirs"
                onClicked: Store.currentPage = "dirs"
            }

            NavItem {
                width: navColumn.width
                glyph: "◇"
                iconSource: "qrc:/img/tag.svg"
                text: Lang.t("tag.library_title")
                count: ConfigBridge.tags.length
                active: Store.currentPage === "tags"
                onClicked: Store.currentPage = "tags"
            }

            Item { width: 1; height: 7 }

            Rectangle {
                width: navColumn.width - 8
                x: 4
                height: 1
                color: Theme.line
            }

            Item { width: 1; height: 7 }

            NavItem {
                width: navColumn.width
                glyph: "⇄"
                iconSource: "qrc:/img/monitor-smartphone.svg"
                text: Lang.t("nav.sync")
                active: Store.currentPage === "sync"
                onClicked: Store.currentPage = "sync"
            }

            NavItem {
                width: navColumn.width
                glyph: "⚙"
                iconSource: "qrc:/img/settings.svg"
                text: Lang.t("nav.settings")
                active: Store.currentPage === "settings"
                onClicked: Store.currentPage = "settings"
            }
        }
    }

    // 窄屏：侧栏变顶栏下方的横向条
    Rectangle {
        id: sidebarBar
        visible: window.compact
        anchors.top: topbar.bottom
        width: parent.width
        height: 42
        color: Theme.surface2

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.line
        }

        Row {
            id: navRow
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            NavItem {
                compact: true
                glyph: "▦"
                iconSource: "qrc:/img/file.svg"
                text: Lang.t("browse.files")
                active: Store.currentPage === "browse"
                onClicked: Store.currentPage = "browse"
            }

            NavItem {
                compact: true
                glyph: "▱"
                iconSource: "qrc:/img/folder.svg"
                text: Lang.t("nav.directory")
                active: Store.currentPage === "dirs"
                onClicked: Store.currentPage = "dirs"
            }

            NavItem {
                compact: true
                glyph: "◇"
                iconSource: "qrc:/img/tag.svg"
                text: Lang.t("tag.library_title")
                active: Store.currentPage === "tags"
                onClicked: Store.currentPage = "tags"
            }

            NavItem {
                compact: true
                glyph: "⇄"
                iconSource: "qrc:/img/monitor-smartphone.svg"
                text: Lang.t("nav.sync")
                active: Store.currentPage === "sync"
                onClicked: Store.currentPage = "sync"
            }

            NavItem {
                compact: true
                glyph: "⚙"
                iconSource: "qrc:/img/settings.svg"
                text: Lang.t("nav.settings")
                active: Store.currentPage === "settings"
                onClicked: Store.currentPage = "settings"
            }
        }
    }

    // 内容区
    Flickable {
        id: contentArea
        anchors.top: window.compact ? sidebarBar.bottom : topbar.bottom
        anchors.left: window.compact ? parent.left : sidebar.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // 固定尺寸：页面级不滚动 滚动交给各页内部列表
        contentWidth: width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: contentInner
            width: Math.min(contentArea.width - window.pad * 2, Theme.contentMax)
            x: Math.max(window.pad, (contentArea.width - width) / 2)
            y: window.pad
            height: pageStack.height

            StackLayout {
                id: pageStack
                width: parent.width

                // 固定尺寸：页面栈直接吃满内容区高度 各页内部自己做固定比例 + 内部滚动
                height: Math.max(0, contentArea.height - window.pad * 2)
                currentIndex: {
                    switch (Store.currentPage) {
                    case "dirs": return 1
                    case "tags": return 2
                    case "sync": return 3
                    case "settings": return 4
                    default: return 0
                    }
                }

                PageBrowse { id: pageBrowsePage }
                PageDirs { id: pageDirsPage }
                PageTags { id: pageTagsPage }
                PageSync { id: pageSyncPage }
                PageSettings { id: pageSettingsPage }
            }
        }
    }

    // Toast
    Rectangle {
        id: toastBox
        z: 60
        visible: opacity > 0
        opacity: 0
        radius: 8
        color: "#22262c"
        height: 32
        width: toastText.implicitWidth + 28
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18

        Behavior on opacity {
            NumberAnimation { duration: 200; easing: Easing.OutCubic }
        }

        Text {
            id: toastText
            anchors.centerIn: parent
            text: ""
            color: "#ffffff"
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
        }

        Timer {
            id: toastTimer
            interval: 1800
            onTriggered: toastBox.opacity = 0
        }
    }

    function showToast(message) {
        toastText.text = message
        toastBox.opacity = 1
        toastTimer.restart()
    }

    Connections {
        target: Store
        function onToastRequested(message) { window.showToast(message) }
        function onTagAssigned(tag, fileName, color) { assignModal.open(tag, fileName, color) }
    }

    // 标签已赋值弹窗
    Rectangle {
        id: assignBackdrop
        anchors.fill: parent
        z: 40
        color: "#4a0f1218"
        visible: opacity > 0
        opacity: 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: assignModal.close()
        }

        Rectangle {
            id: assignCard
            width: Math.min(480, assignBackdrop.width - 32)
            height: modalColumn.height
            anchors.centerIn: parent
            radius: 12
            color: Theme.surface
            border.width: 1
            border.color: Theme.line2

            Column {
                id: modalColumn
                width: parent.width

                Item {
                    width: parent.width
                    height: 46

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: Lang.t("file.tag_assigned")
                        font.pixelSize: Theme.px(13)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "x"
                        font.pixelSize: Theme.px(16)
                        color: Theme.text3

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: assignModal.close()
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.line
                    }
                }

                Column {
                    width: parent.width
                    spacing: 8
                    topPadding: 14
                    leftPadding: 14
                    rightPadding: 14

                    Rectangle {
                        width: parent.width - 28
                        height: 40
                        radius: 8
                        color: Theme.surface2
                        border.width: 1
                        border.color: Theme.line

                        Row {
                            anchors.centerIn: parent
                            spacing: 8

                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: assignModal.tagColor
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: assignModal.tagName
                                font.pixelSize: Theme.px(12)
                                font.weight: Font.Bold
                                font.family: Theme.fontFamily
                                color: Theme.text
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "->"
                                font.pixelSize: Theme.px(12)
                                color: Theme.text3
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: assignModal.fileName
                                font.pixelSize: Theme.px(12)
                                font.family: Theme.fontFamily
                                color: Theme.text2
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }

                    Text {
                        width: parent.width - 28
                        text: Lang.t("file.tag_assigned_note")
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.px(12)
                        font.family: Theme.fontFamily
                        color: Theme.text2
                    }
                }

                Item {
                    width: parent.width
                    height: 52

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.line
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Btn { text: Lang.t("common.know"); onClicked: assignModal.close() }
                        Btn { text: Lang.t("common.done"); kind: "primary"; onClicked: assignModal.close() }
                    }
                }
            }
        }
    }

    QtObject {
        id: assignModal

        property string tagName: ""
        property string fileName: ""
        property color tagColor: Theme.accent

        function open(tag, file, color) {
            tagName = tag
            fileName = file
            tagColor = color
            assignBackdrop.opacity = 1
        }

        function close() {
            assignBackdrop.opacity = 0
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: assignModal.close()
    }
}
