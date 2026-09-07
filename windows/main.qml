import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import "control"

ApplicationWindow {
    id: window
    width: 800
    height: 600
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: qsTr("TagMeow")
    color: "#f0f2f5"

    // 语言字典: id -> 当前语言文本 由 Bridge(pushUiText) 从 LanguageManager 填充
    property var uiText: ({})
    // 可用语言列表: 由 Bridge.pushLanguageList 在初始化时按语言文件目录填充(同 test.cpp)                                                                                                
    property var languageNames: []

    // 这个 Item 作为拖拽元素的临时父级 所有放到这里的元素都会显示在最顶层
    Item {
        id: dragOverlay
        anchors.fill: parent
        z: 9999
        visible: false
    }

    Window {
        id: setWindow
        width: 400
        height: 300
        minimumWidth: 400
        maximumWidth: 400
        minimumHeight: 300
        maximumHeight: 300
        title: ""
        modality: Qt.ApplicationModal
        visible: false
        color: "#ffffff"

        GridLayout {
            anchors.fill: parent
            anchors.margins: 10
            columns: 2
            rowSpacing: 8
            columnSpacing: 10

            Label { text: window.uiText["settings.version"] }
            Label {
                objectName: "versionValue"
                text: "beta"
            }

            Label { text: window.uiText["settings.port"] }
            Label {
                objectName: "portValue"
                text: "11451"
            }

            Label { text: window.uiText["settings.magic"] }
            Label {
                objectName: "magicValue"
                text: "0x114514"
            }

            Label { text: window.uiText["settings.tagMode"] }
            Label {
                objectName: "tagModeValue"
                text: "Sidecar"
            }

            Label { text: window.uiText["settings.wait"] }
            SpinBox {
                objectName: "waitSpin"
                from: 0
                to: 1440
                value: 5
                Layout.fillWidth: true
            }

            Label { text: window.uiText["settings.download"] }
            TextField {
                objectName: "downloadPath"
                text: "./download"
                Layout.fillWidth: true
            }

            Label { text: window.uiText["settings.language"] }
            ComboBox {
                id: languageCombo
                objectName: "languageCombo"
                model: window.languageNames
                currentIndex: 0
                Layout.fillWidth: true
                onCurrentTextChanged: {
                    // 语言在确认时由 Bridge 读取并保存 重启后生效
                    console.log("Language selected:", currentText)
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: window.uiText["settings.restartTip"]
                color: "#a0aec0"
                font.pointSize: 9
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 20

                Button {
                    objectName: "saveConfigBtn"
                    text: window.uiText["btn.ok"]
                    onClicked: {
                        // Bridge.onSaveConfigClicked: 读取控件保存 config.json 并退出程序(重启生效)
                        setWindow.close()
                    }
                }
                Button {
                    text: window.uiText["btn.cancel"]
                    onClicked: {
                        setWindow.close()
                    }
                }
            }
        }
    }

    Window {
        id: optionsWindow
        width: 280
        height: 140
        minimumWidth: 280
        maximumWidth: 280
        minimumHeight: 140
        maximumHeight: 140
        title: ""
        modality: Qt.ApplicationModal
        visible: false
        color: "#ffffff"

        Dialog {
            id: optionsDialog
            objectName: "optionsDialog"
            title: window.uiText["options.title"]
            modal: true
            standardButtons: Dialog.Yes | Dialog.No
            onAccepted: {
                // 转换由 Bridge.onConvertModeConfirmed 执行
                console.log("确认")
                optionsWindow.close()
            }
            onRejected: {
                console.log("取消")
                optionsWindow.close()
            }
        }

        onVisibleChanged: {
            if (visible) {
                optionsDialog.open()
            }
        }
    }

    Window {
        id: syncWindow
        objectName: "syncWindow"
        width: 400
        height: 300
        minimumWidth: 400
        maximumWidth: 400
        minimumHeight: 300
        maximumHeight: 300
        title: ""
        modality: Qt.NonModal
        visible: false
        color: "#ffffff"

        // 由 Bridge(C++) 填充/绑定的数据与操作
        property var serverList: []   // rect3 局域网设备列表 [{name, ip, port}]
        property var serverQueue: []  // rect2 待发送目录队列 [路径...]

        Rectangle {
            id: mainRect
            anchors.fill: parent
            color: "#FFFFFF"
            visible: true

            RowLayout {
                anchors.centerIn: parent
                spacing: 32

                PictureButton {
                    buttonWidth: 64
                    buttonHeight: 64
                    iconWidth: 64
                    iconHeight: 64
                    iconSource: "/img/database-plus.svg"

                    ToolTip.visible: hovered
                    ToolTip.text: window.uiText["sync.share"]

                    onClicked: {
                        rect2.visible = true
                        mainRect.visible = false
                        rect3.visible = false
                    }
                }

                PictureButton {
                    buttonWidth: 64
                    buttonHeight: 64
                    iconWidth: 64
                    iconHeight: 64
                    iconSource: "/img/download.svg"

                    ToolTip.visible: hovered
                    ToolTip.text: window.uiText["sync.download"]

                    onClicked: {
                        rect3.visible = true
                        mainRect.visible = false
                        rect2.visible = false
                    }
                }
            }
        }

        Rectangle {
            id: rect2
            anchors.fill: parent
            color: "#FFFFFF"
            visible: false

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    spacing: 5

                    TextField {
                        id: serverNameField
                        objectName: "serverNameField"
                        placeholderText: window.uiText["sync.serverName"]
                        Layout.fillWidth: true
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: 0

                        PictureButton {
                            objectName: "serverStartBtn"
                            buttonWidth: 50
                            buttonHeight: 50
                            iconWidth: 50
                            iconHeight: 50
                            iconSource: "/img/power.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["sync.start"]
                        }

                        PictureButton {
                            objectName: "serverStopBtn"
                            buttonWidth: 50
                            buttonHeight: 50
                            iconWidth: 50
                            iconHeight: 50
                            iconSource: "/img/power-off.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["sync.stop"]
                        }
                    }
                }

                // 第二行：目录 + 添加
                RowLayout {
                    spacing: 5
                    TextField {
                        id: dirField
                        objectName: "serverDirField"
                        placeholderText: window.uiText["sync.dirPath"]
                        Layout.fillWidth: true
                    }

                    PictureButton {
                        objectName: "serverAddDirBtn"
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/folder-plus.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.addDir"]
                    }
                }

                //  每一步操作完成后的提示由下方状态栏给出
                Item { Layout.fillHeight: true }

                Label {
                    objectName: "serverStatusLabel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    color: "#a0aec0"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: ""
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter

                    Row {
                        anchors.centerIn: parent
                        spacing: 10

                        PictureButton {
                            objectName: "serverDisconnectBtn"
                            buttonWidth: 50
                            buttonHeight: 50
                            iconWidth: 50
                            iconHeight: 50
                            iconSource: "/img/unplug.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["sync.disconnect"]
                        }

                        PictureButton {
                            buttonWidth: 50
                            buttonHeight: 50
                            iconWidth: 50
                            iconHeight: 50
                            iconSource: "/img/arrow-big-left.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["sync.back"]

                            onClicked: {
                                mainRect.visible = true
                                rect2.visible = false
                                rect3.visible = false
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: rect3
            anchors.fill: parent
            color: "#FFFFFF"
            visible: false

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                // 设备列表(由 Bridge.pushServerList 填充): 单击选中 底部"下载"按钮下载选中设备
                ListView {
                    id: serverListView
                    objectName: "serverListView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: syncWindow.serverList
                    clip: true

                    delegate: Rectangle {
                        width: ListView.view ? ListView.view.width : 0
                        height: 26
                        radius: 4
                        color: serverListView.currentIndex === index ? "#ebf8ff" : (index % 2 ? "#f7fafc" : "transparent")

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                serverListView.currentIndex = index
                            }
                        }

                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.name + "    " + modelData.ip + ":" + modelData.port
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Label {
                    objectName: "clientStatusLabel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    color: "#a0aec0"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: ""
                }

                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10

                    PictureButton {
                        objectName: "clientScanBtn"
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/search.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.scan"]
                    }

                    PictureButton {
                        objectName: "clientDownloadBtn"
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/download.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.downloadSel"]
                    }

                    PictureButton {
                        objectName: "clientClearBtn"
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/trash.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.clearRecords"]
                    }

                    PictureButton {
                        objectName: "clientDisconnectBtn"
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/unplug.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.disconnect"]
                    }

                    PictureButton {
                        buttonWidth: 50
                        buttonHeight: 50
                        iconWidth: 50
                        iconHeight: 50
                        iconSource: "/img/arrow-big-left.svg"

                        ToolTip.visible: hovered
                        ToolTip.text: window.uiText["sync.back"]

                        onClicked: {
                            mainRect.visible = true
                            rect2.visible = false
                            rect3.visible = false
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id:titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#f8f9fc"

            PictureButton {
                id: settingButton
                anchors.left: parent.left
                anchors.top: parent.top
                iconSource: "/img/settings.svg"
                onClicked: {
                    setWindow.show()
                }
            }

            PictureButton {
                id: optionsButton
                anchors.left: settingButton.right
                anchors.top: parent.top
                iconSource: "/img/settings-2.svg"
                onClicked: {
                    optionsWindow.show()
                }
            }

            PictureButton {
                id: helpButton
                anchors.left: optionsButton.right
                anchors.top: parent.top
                iconSource: "/img/circle-question-mark.svg"
                onClicked: {
                    Qt.openUrlExternally("https://github.com/kakameow/TagMeow")
                }
            }

            DataDisplayLabel {
                id: fileLabel
                anchors.right: tagLabel.left
                anchors.top: parent.top
                iconSource: "/img/file.svg"
                tipText: window.uiText["tip.file"]
                // 实时显示下方文件列表的文件数量
                dataText: fileContainer.fileCount
            }

            DataDisplayLabel {
                id: tagLabel
                anchors.right: parent.right
                anchors.top: parent.top
                iconSource: "/img/tag.svg"
                tipText: window.uiText["tip.tag"]
                // 实时显示下方标签库的标签总数
                dataText: libraryTag.totalTagCount
            }
        }


        Rectangle {
            id: searchBar
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: "#f0f2f5"

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 8
                    color: "#f0f2f5"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 10

                        TagContainer {
                            id: includeContainer
                            objectName: "includeContainer"
                            Layout.fillHeight: true
                            Layout.preferredWidth: 192
                            containerName: window.uiText["cnt.include"]
                            containerTip: window.uiText["cnt.tip"]
                            borderColor: "#48bb78"
                            backgroundColor: "#f0fff4"

                        }

                        TagContainer {
                            id: excludeContainer
                            objectName: "excludeContainer"
                            Layout.fillHeight: true
                            Layout.preferredWidth: 192
                            containerName: window.uiText["cnt.exclude"]
                            containerTip: window.uiText["cnt.tip"]
                            borderColor: "#fc8181"
                            backgroundColor: "#fff5f5"

                        }

                        TagContainer {
                            id: onlyContainer
                            objectName: "onlyContainer"
                            Layout.fillHeight: true
                            Layout.preferredWidth: 192
                            containerName: window.uiText["cnt.only"]
                            containerTip: window.uiText["cnt.tip"]
                            borderColor: "#63b3ed"
                            backgroundColor: "#ebf8ff"

                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        PictureButton {
                            id: searchButton
                            objectName: "searchButton"
                            anchors.right: clearButton.left
                            buttonWidth: 64
                            buttonHeight: 64
                            iconSource: "/img/search.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["btn.search"]
                        }

                        PictureButton {
                            id: clearButton
                            objectName: "clearButton"
                            anchors.right: refreshButton.left
                            buttonWidth: 64
                            buttonHeight: 64
                            iconSource: "/img/trash.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["btn.clear"]
                        }

                        PictureButton {
                            id: refreshButton
                            objectName: "refreshButton"
                            anchors.right: parent.right
                            buttonWidth: 64
                            buttonHeight: 64
                            iconSource: "/img/rotate-cw.svg"

                            ToolTip.visible: hovered
                            ToolTip.text: window.uiText["btn.refresh"]
                        }
                    }
                }
            }
        }

        Rectangle {
            id: middleLayout
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f0f2f5"

            RowLayout {
                anchors.fill: parent
                spacing: 0


                Rectangle {
                    id: lefttBar
                    Layout.fillHeight: true
                    Layout.preferredWidth: 192
                    color: "#f0f2f5"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            id: dirList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 2
                            color: "#FFFFFF"

                            DirContainer {
                                id: dirContainer
                                objectName: "dirContainer"
                                anchors.fill: parent
                                iconSource: "/img/folder.svg"
                            }
                        }

                        Rectangle {
                            id: tagList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 3
                            color: "#FFFFFF"

                            LibraryTag {
                                id: libraryTag
                                objectName: "libraryTag"
                                anchors.fill: parent
                            }
                        }
                    }
                }

                Rectangle {
                    id: rightBar
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#FFFFFF"

                    FileContainer {
                        id: fileContainer
                        objectName: "fileContainer"
                        itemWidth: parent.width

                    }
                }
            }
        }

        Rectangle {
            id: optionsBar
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "#f8f9fc"

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    anchors.left: parent.left
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            width: 144
                            height: 64
                            anchors.left: parent.left

                            PictureTextFieldButton {
                                id: dirInput
                                objectName: "dirInput"
                                itemWidth: parent.width
                                itemHeight: parent.height
                                iconSource: "/img/folder.svg"
                            }
                        }

                        Rectangle {
                            width: 84
                            height: 64
                            anchors.left: dirInput.right

                            PictureTextFieldButton {
                                id: typeInput
                                objectName: "typeInput"
                                itemWidth: parent.width
                                itemHeight: parent.height
                                iconSource: "/img/funnel.svg"
                            }
                        }

                        Rectangle {
                            width: 84
                            height: 64
                            anchors.left: typeInput.right

                            PictureTextFieldButton {
                                id: tagInput
                                objectName: "tagInput"
                                itemWidth: parent.width
                                itemHeight: parent.height
                                iconSource: "/img/tag.svg"
                            }
                        }


                        Rectangle {
                            width: 84
                            height: 64
                            anchors.left: tagInput.right

                            Rectangle {
                                id: colorRect
                                objectName: "colorInput"
                                anchors.centerIn: parent
                                width: parent.width * 0.6
                                height: parent.height * 0.6
                                radius: 8
                                color: currentColor

                                property string currentColor: "#FFB6C1"
                                property string text: currentColor

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: colorDialog.open()
                                }
                            }

                            ColorDialog {
                                id: colorDialog
                                selectedColor: colorRect.currentColor
                                onAccepted: {
                                    colorRect.currentColor = colorDialog.selectedColor
                                    console.log("选择的颜色:", colorDialog.selectedColor)
                                }
                                onRejected: {
                                    console.log("取消选择")
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    anchors.right: parent.right
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0


                        Rectangle {
                            id: button1
                            width: 48
                            height: 64
                            anchors.right: button2.left
                            color: "transparent"

                            PictureButton {
                                objectName: "addDirBtn"
                                iconSource: "/img/folder-plus.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.addDir"]
                            }
                        }

                        Rectangle {
                            id: button2
                            width: 48
                            height: 64
                            anchors.right: button3.left
                            color: "transparent"

                            PictureButton {
                                objectName: "addTypeBtn"
                                iconSource: "/img/funnel-plus.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.addType"]
                            }
                        }

                        Rectangle {
                            id: button3
                            width: 48
                            height: 64
                            anchors.right: button4.left
                            color: "transparent"

                            PictureButton {
                                objectName: "addTagBtn"
                                iconSource: "/img/tag-plus.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.addTag"]
                            }
                        }


                        Rectangle {
                            id: button4
                            width: 48
                            height: 64
                            anchors.right: button5.left
                            color: "transparent"

                            PictureButton {
                                objectName: "removeDirBtn"
                                iconSource: "/img/folder-x.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.removeDir"]
                            }
                        }

                        Rectangle {
                            id: button5
                            width: 48
                            height: 64
                            anchors.right: button6.left
                            color: "transparent"

                            PictureButton {
                                objectName: "removeTypeBtn"
                                iconSource: "/img/funnel-x.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.removeType"]
                            }
                        }

                        Rectangle {
                            id: button6
                            width: 48
                            height: 64
                            anchors.right: button7.left
                            color: "transparent"

                            PictureButton {
                                objectName: "removeTagBtn"
                                iconSource: "/img/tag-x.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.removeTag"]
                            }
                        }

                        Rectangle {
                            id: button7
                            width: 48
                            height: 64
                            anchors.right: endButton.left
                            color: "transparent"

                            PictureButton {
                                objectName: "resetTypeColor"
                                iconSource: "/img/paint-roller.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.resetColor"]
                            }
                        }

                        Rectangle {
                            id: endButton
                            width: 48
                            height: 64
                            anchors.right: parent.right
                            color: "transparent"

                            PictureButton {
                                objectName: "snycWindow"
                                iconSource: "/img/monitor-smartphone.svg"
                                buttonWidth: parent.width
                                buttonHeight: parent.height
                                iconWidth: 48
                                iconHeight: 48

                                ToolTip.visible: hovered
                                ToolTip.text: window.uiText["btn.syncWindow"]

                                onClicked: {
                                    syncWindow.show()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
