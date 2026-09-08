pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    property string backgroundColor: "transparent"
    property string borderColor: "#e2e6ee"
    property string iconSource: ""
    property int iconSize: 16
    property int itemWidth: 192
    property int itemHeight: 32
    property int maxRow: 6
    property int fontSize: 12
    property var dirList: []

    signal dirDoubleClicked(string text)

    onDirListChanged: updateDirModel()

    function updateDirModel()
    {
        dirListModel.clear()
        for (var i = 0; i < dirList.length; i++)
        {
            dirListModel.append(dirList[i])
        }
    }

    ListModel { id: dirListModel }

    function addDir(text)
    {
        for (var i = 0; i < dirList.length; i++)
        {
            if (dirList[i].text === text)
            {
                console.log("Tag with same text already exists:", text)
                return false
            }
        }
        var newList = dirList.slice()
        newList.push({text: text })
        dirList = newList
        console.log("Dir added:", text)
        return true
    }

    function removeDir(text)
    {
        var index = -1
        for (var i = 0; i < dirList.length; i++)
        {
            if (dirList[i].text === text)
            {
                index = i
                break
            }
        }
        if (index >= 0)
        {
            var newList = dirList.slice()
            newList.splice(index, 1)
            dirList = newList
            console.log("Dir removed with text:", text)
        }
        else
        {
            console.log("No tag with text:", text)
        }
    }

    width: itemWidth
    height: itemHeight * maxRow
    color: backgroundColor
    border.color: borderColor
    border.width: 1
    radius: 16
    clip: true

    ListView {
        id: dirListView
        anchors.fill: parent
        model: dirListModel
        delegate: Item {
            id: dirItem
            required property var model

            width: root.itemWidth
            height: root.itemHeight

            property bool hovered: false

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: dirItem.hovered = true
                onExited: dirItem.hovered = false
                onDoubleClicked: root.dirDoubleClicked(dirItem.model.text)
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.fillHeight: true
                    color: "transparent"

                    Image {
                        anchors.centerIn: parent
                        width: root.iconSize
                        height: root.iconSize
                        source: root.iconSource
                        fillMode: Image.PreserveAspectFit
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"

                    Label {
                        id: textLabel
                        anchors.fill: parent
                        text: dirItem.model.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: root.fontSize
                        elide: Text.ElideRight
                        clip: true
                    }

                    // 悬停下划线：跟随文字宽度 悬停淡入 移出淡出
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        width: Math.min(textLabel.contentWidth, parent.width)
                        height: 1
                        color: "black"
                        opacity: dirItem.hovered ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation { duration: 150 }
                        }
                    }
                }
            }
        }
    }
}