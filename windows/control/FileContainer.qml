pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

Rectangle {
    id: root
    property var fileList: []
    // 文件数量（fileList 变化时实时更新）
    property int fileCount: fileList.length
    property string backgroundColor: "transparent"
    property string borderColor: "#e2e6ee"
    property string fileColor: "#ffffff"
    property string tagColor: "#FFB6C1"
    property string tagBackgroundColor: "#ffffff" // 行内标签背景色 可外部修改
    property string tagTextColor: "black" // 行内标签文字色(可外部修改 随主题)
    property int itemWidth: 260
    property int itemHeight: 28
    property int tagWidth: 64
    property int tagColumns: 5
    property int fontSize: 16

    signal fileDoubleClicked(string text)
    // 标签变更信号（拼接路径与标签）
    signal fileTagAdded(string filePath, string tag)
    signal fileTagRemoved(string filePath, string tag)
    signal fileTagChanged(string filePath, string oldTag, string newTag)

    function normalizeTags(raw)
    {
        var out = []
        if (raw === undefined || raw === null)
        {
            return out
        }
        if (typeof raw === "string")
        {
            out.push(raw)
        }
        else if (Array.isArray(raw))
        {
            for (var i = 0; i < raw.length; i++)
            {
                out.push(raw[i])
            }
        }
        else if (typeof raw === "object")
        {
            for (var key in raw)
            {
                var v = raw[key]
                if (typeof v === "string" && v.charAt(0) === "#")
                {
                    out.push({ text: key, color: v })
                }
                else
                {
                    out.push(key)
                }
            }
        }
        return out
    }

    // 追加一个文件（按 text 去重 不重复添加）
    function addFile(text, tags)
    {
        if (!text)
        {
            return false
        }
        for (var i = 0; i < fileList.length; i++)
        {
            if (fileList[i].text === text)
            {
                return false
            }
        }
        var newList = fileList.slice()
        newList.push({ text: text, tags: normalizeTags(tags) })
        fileList = newList
        return true
    }

    // 按文件路径删除
    function removeFile(text)
    {
        for (var i = 0; i < fileList.length; i++)
        {
            if (fileList[i].text === text)
            {
                var newList = fileList.slice()
                newList.splice(i, 1)
                fileList = newList
                return true
            }
        }
        return false
    }

    function getDisplayPath(path)
    {
        if (!path || path.length === 0)
        {
            return ""
        }

        var clean = path.replace(/[\/\\]+$/, "")
        var parts = clean.split(/[\/\\]/)
        var last = parts[parts.length - 1]

        if (last.includes(".") && last !== "." && last !== "..")
        {
            return last
        }
        else
        {
            return path
        }
    }

    width: itemWidth
    height: parent.height
    color: backgroundColor
    border.color: borderColor
    border.width: 1
    radius: 8
    clip: true

    ListView {
        id: fileListView
        anchors.fill: parent
        model: root.fileList
        clip: true

        delegate: Item {
            id: fileItem
            required property var modelData

            width: root.width
            height: root.itemHeight

            // 该文件的标签 -> 行内 TagContainer 可用的 {color,text} 列表
            property var fileTagList:
            {
                var out = []
                var raw = fileItem.modelData.tags ? fileItem.modelData.tags : []
                for (var i = 0; i < raw.length; i++)
                {
                    var t = raw[i]
                    if (typeof t === "string")
                    {
                        out.push({ color: root.tagColor, text: t })
                    }
                    else
                    {
                        out.push({ color: t.color ? t.color : root.tagColor, text: t.text ? t.text : "" })
                    }
                }
                return out
            }

            Rectangle {
                id: leftPath
                width: itemWidth - (tagColumns * tagWidth)
                height: itemHeight
                color: root.fileColor

                Label {
                    anchors.fill: parent
                    anchors.margins: 4
                    text: root.getDisplayPath(modelData.text)
                    font.pixelSize: root.fontSize
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    clip: true
                }

                // 双击该行 -> 发送文件路径
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.fileDoubleClicked(fileItem.modelData.text)
                }
            }

            Rectangle {
                width: tagColumns * tagWidth
                height: itemHeight
                anchors.left: leftPath.right
                color: root.fileColor

                TagContainer {
                    id: fileTags
                    anchors.centerIn: parent
                    backgroundColor: root.fileColor
                    borderColor: "transparent"
                    maxRow: 1
                    headHeight: 0
                    containerTip: ""
                    tagWidth: root.tagWidth
                    maxLine: root.tagColumns
                    tagBackgroundColor: root.tagBackgroundColor
                    tagTextColor: root.tagTextColor
                    tagList: fileItem.fileTagList

                    // 标签变更 -> FileContainer 拼接"路径 + 标签"
                    onTagAdded: function(text) {
                        root.fileTagAdded(fileItem.modelData.text, text)
                    }
                    onTagRemoved: function(text) {
                        root.fileTagRemoved(fileItem.modelData.text, text)
                    }
                    onTagChanged: function(oldText, newText) {
                        root.fileTagChanged(fileItem.modelData.text, oldText, newText)
                    }
                }
            }
        }
    }
}
