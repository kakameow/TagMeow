pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    // 类型列表数据 { typeName: string, color: string, tags: [text, ...] }
    property var typeList: []
    // 所有类型下的标签总数（typeList 变化时实时更新）
    property int totalTagCount:
    {
        var count = 0
        for (var i = 0; i < typeList.length; i++)
        {
            count += (typeList[i].tags ? typeList[i].tags.length : 0)
        }
        return count
    }
    property string backgroundColor: "transparent"
    property string borderColor: "#e2e6ee"
    property string gridBorderColor: "#cccccc"
    property int maxRow: 2
    property int maxLine: 3
    property int tagWidth: 64
    property int tagHeight: 24

    width: tagWidth * maxLine
    height: parent.height
    color: backgroundColor
    border.color: borderColor
    border.width: 1
    clip: true

    // 添加类型（不重复）color 为该类型下所有标签的颜色
    function addType(typeName, color)
    {
        if (!typeName)
        {
            return false
        }
        if (typeName === "")
        {
            return false
        }
        for (var i = 0; i < typeList.length; i++)
        {
            if (typeList[i].typeName === typeName)
            {
                return false
            }
        }
        var list = typeList.slice()
        list.push({ typeName: typeName, color: color || "#FFB6C1", tags: [] })
        typeList = list
        return true
    }

    // 修改类型的颜色（该类型下所有标签同步变色）
    function updateTypeColor(typeName, color)
    {
        for (var i = 0; i < typeList.length; i++)
        {
            if (typeList[i].typeName === typeName)
            {
                var newList = typeList.slice()
                newList[i] = { typeName: typeName, color: color, tags: newList[i].tags.slice() }
                typeList = newList
                return true
            }
        }
        return false
    }

    // 删除类型（连带其中所有标签）
    function removeType(typeName)
    {
        var list = []
        for (var i = 0; i < typeList.length; i++)
        {
            if (typeList[i].typeName !== typeName)
                list.push(typeList[i])
        }
        if (list.length === typeList.length)
        {
            return false
        }
        typeList = list
        return true
    }

    // 向指定类型添加标签 标签按文字全局唯一 不添加已有的
    function addTag(typeName, tagText)
    {
        if (!typeName || !tagText)
        {
            return false
        }
        if (tagText === "" || typeName === "")
        {
            return false
        }

        for (var i = 0; i < typeList.length; i++)
        {
            for (var j = 0; j < typeList[i].tags.length; j++)
            {
                if (typeList[i].tags[j] === tagText)
                {
                    updateTag(typeName, tagText)
                    return true
                }
            }
        }
        for (var k = 0; k < typeList.length; k++)
        {
            if (typeList[k].typeName === typeName)
            {
                var newList = typeList.slice()
                var newTags = newList[k].tags.slice()
                newTags.push(tagText)
                newList[k] = { typeName: typeName, color: newList[k].color, tags: newTags }
                typeList = newList
                return true
            }
        }
        return false
    }

    // 删除指定类型下的一个标签
    function removeTag(typeName, tagText)
    {
        for (var i = 0; i < typeList.length; i++)
        {
            if (typeList[i].typeName === typeName)
            {
                var idx = typeList[i].tags.indexOf(tagText)
                if (idx < 0)
                {
                    return false
                }
                var newList = typeList.slice()
                var newTags = newList[i].tags.slice()
                newTags.splice(idx, 1)
                newList[i] = { typeName: typeName, color: newList[i].color, tags: newTags }
                typeList = newList
                return true
            }
        }
        return false
    }

    // 修改指定类型下某个标签的文字
    function updateTag(typeName, tagText, newText)
    {
        if (!newText)
        {
            return false
        }
        for (var i = 0; i < typeList.length; i++)
        {
            if (typeList[i].typeName === typeName)
            {
                var idx = typeList[i].tags.indexOf(tagText)
                if (idx < 0)
                    return false
                if (newText !== tagText)
                {
                    for (var k = 0; k < typeList.length; k++)
                    {
                        if (typeList[k].tags.indexOf(newText) >= 0)
                        {
                            return false
                        }
                    }
                }
                var newList = typeList.slice()
                var newTags = newList[i].tags.slice()
                newTags[idx] = newText
                newList[i] = { typeName: typeName, color: newList[i].color, tags: newTags }
                typeList = newList
                return true
            }
        }
        return false
    }

    ListView {
        id: typeListView
        anchors.fill: parent
        model: root.typeList
        delegate: Item {
            id: typeItem
            required property int index
            required property var modelData

            width: parent.width
            height: expanded ? headerHeight + root.tagHeight * root.maxRow : headerHeight

            property bool expanded: true
            property string typeName: modelData.typeName
            property string typeColor: modelData.color ? modelData.color : "#FFB6C1"
            property var tags: modelData.tags ? modelData.tags : []
            property int headerHeight: 16

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // 类型名行（点击展开/收起）
                Rectangle {
                    id: header
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    color: "transparent"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: typeItem.expanded = !typeItem.expanded
                    }

                    Label {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 10
                        anchors.topMargin: 1
                        text: (typeItem.expanded ? "▼ " : "▶ ") + typeItem.typeName
                        font.pixelSize: 14
                    }
                    Label {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 10
                        anchors.topMargin: 1
                        text: typeItem.tags.length
                        font.pixelSize: 14
                    }
                }

                // 标签网格
                Rectangle {
                    id: gridBody
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: root.tagHeight * root.maxRow
                    color: "transparent"
                    border.color: typeItem.typeColor
                    border.width: 1
                    radius: 16
                    clip: true

                    GridView {
                        id: tagGridView
                        anchors.fill: parent
                        model: typeItem.tags
                        delegate: Item {
                            id: tagItem
                            required property var modelData
                            width: root.tagWidth
                            height: root.tagHeight

                            TagRectangle {
                                width: root.tagWidth
                                height: root.tagHeight
                                tagColor: typeItem.typeColor
                                tagText: tagItem.modelData
                            }
                        }
                        cellWidth: root.tagWidth
                        cellHeight: root.tagHeight
                        flow: GridView.FlowLeftToRight
                        clip: true

                        // 标签列表为 0 的类型：条目仍然显示，网格区内给出占位提示
                        Label {
                            anchors.centerIn: parent
                            text: "（空）"
                            font.pixelSize: 10
                            color: "#999999"
                            visible: typeItem.tags.length === 0
                        }
                    }
                }
            }
        }
    }
}
