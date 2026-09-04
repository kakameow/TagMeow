pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Rectangle {
    id: container
    property string containerName: "标签容器"
    property string containerTip: "拖拽标签到这里"
    property string backgroundColor: "transparent"
    property string borderColor: "red"
    property string dropHighlightColor: "#2a82da"
    property int maxRow: 3
    property int maxLine: 3
    property int headHeight: 14
    property int tagWidth: 64
    property int tagHeight: 24
    property var tagList: []

    // 标签变更信号（增/删/改），供外部（如 FileContainer）接收后拼接文件路径发给后端
    signal tagAdded(string text)
    signal tagRemoved(string text)
    signal tagChanged(string oldText, string newText)

    onWidthChanged:
    {
        if (visible)
        {
            update()
        }
    }

    width: tagWidth * maxLine
    height: tagHeight * maxRow + headHeight
    color: backgroundColor
    border.color: dropArea.containsDrag ? dropHighlightColor : borderColor
    border.width: 1
    radius: 16
    clip: true

    onTagListChanged: updateTagModel()

    function updateTagModel()
    {
        tagListModel.clear()
        for (var i = 0; i < tagList.length; i++)
        {
            tagListModel.append(tagList[i])
        }
    }

    ListModel { id: tagListModel }

    Component.onCompleted: updateTagModel()

    // 复制一份标签内容加入渲染列表 返回是否成功添加（已存在同名标签时返回 false）
    function addTag(color, text)
    {
        // 仅根据 text 判断是否已存在
        for (var i = 0; i < tagList.length; i++)
        {
            if (tagList[i].text === text)
            {
                console.log("Tag with same text already exists:", text)
                return false
            }
        }
        var newList = tagList.slice()
        newList.push({ color: color, text: text })
        tagList = newList
        console.log("Tag added:", text)
        tagAdded(text)
        return true
    }

    function removeTag(text)
    {
        var index = -1
        for (var i = 0; i < tagList.length; i++)
        {
            if (tagList[i].text === text)
            {
                index = i
                break
            }
        }
        if (index >= 0)
        {
            var newList = tagList.slice()
            newList.splice(index, 1)
            tagList = newList
            console.log("Tag removed with text:", text)
            tagRemoved(text)
            return true
        }
        console.log("No tag with text:", text)
        return false
    }

    // 修改标签文字（返回是否成功），成功后发 tagChanged(oldText, newText)
    function updateTag(oldText, newText)
    {
        var index = -1
        for (var i = 0; i < tagList.length; i++)
        {
            if (tagList[i].text === oldText)
            {
                index = i
                break
            }
        }
        if (index < 0)
            return false
        for (var j = 0; j < tagList.length; j++)
        {
            if (j !== index && tagList[j].text === newText)
            {
                return false
            }
        }
        var newList = tagList.slice()
        newList[index] = { color: newList[index].color, text: newText }
        tagList = newList
        tagChanged(oldText, newText)
        return true
    }

    // 接收外部拖拽进入的 TagRectangle：复制其内容并加入渲染列表（addTag）
    // keys 留空：接受任意拖拽源（TagRectangle 内部拖拽 / LibraryTag 平台级拖拽）
    DropArea {
        id: dropArea
        anchors.fill: parent

        onDropped: function(drop)
        {
            drop.accepted = true
            var color = ""
            var text = ""
            var src = drop.source
            if (src && typeof src.tagColor === "string" && typeof src.tagText === "string")
            {
                // 内部拖拽：直接读取来源元素的属性
                color = src.tagColor
                text = src.tagText
            }
            else
            {
                // 平台级拖拽（如 LibraryTag）：从 mimeData 读取
                text = drop.getDataAsString("text/plain")
                color = drop.getDataAsString("text/color")
            }
            if (text)
            {
                container.addTag(color, text)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: container.headHeight
            color: "transparent"
            visible: container.headHeight > 0

            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.topMargin: 1
                text: container.containerName
                font.pixelSize: 12
            }
            Label {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 10
                anchors.topMargin: 1
                text: container.tagList.length
                font.pixelSize: 12
            }
        }

        Rectangle {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: container.containerTip
                font.pixelSize: 8
                visible: container.tagList.length === 0
            }

            GridView {
                id: tagGridView
                anchors.fill: parent
                model: tagListModel
                // 外层用 Item 作为 delegate（GridView 只管理它） 内层 TagRectangle
                // 拖拽时可临时改挂到窗口顶层实现置顶且不被容器裁剪
                delegate: Item {
                    id: tagItem
                    required property var model
                    width: container.tagWidth
                    height: container.tagHeight

                    TagRectangle {
                        width: container.tagWidth
                        height: container.tagHeight
                        tagColor: tagItem.model.color
                        tagText: tagItem.model.text
                        onExitedParent: container.removeTag(tagItem.model.text)
                    }
                }
                cellWidth: container.tagWidth
                cellHeight: container.tagHeight
                flow: GridView.FlowLeftToRight
                clip: true
            }
        }
    }
}
