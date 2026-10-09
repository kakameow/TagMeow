import QtQuick

Item {
    id: head

    property string eyebrow: ""
    property string title: ""
    property string desc: ""
    property bool stacked: false
    property int actionsRightMargin: 0 // 右侧操作区留白
    property bool actionsAlignToTitle: false
    default property alias actions: actionRow.data

    implicitHeight: stacked ? (textCol.height + 8 + (actionRow.visible ? actionRow.height : 0)) : Math.max(textCol.height, actionRow.visible ? 30 : 0)
    height: implicitHeight

    Column {
        id: textCol
        width: head.stacked ? parent.width : Math.min(parent.width, 620)
        spacing: 0

        Text {
            text: head.eyebrow
            font.pixelSize: Theme.px(10)
            font.family: Theme.fontFamily
            color: Theme.text3
        }

        Text {
            id: titleText
            text: head.title
            font.pixelSize: Theme.px(19)
            font.weight: Font.DemiBold
            font.family: Theme.fontFamily
            color: Theme.text
        }

        Text {
            visible: head.desc.length > 0
            text: head.desc
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            color: Theme.text2
            topPadding: 3
        }
    }

    // 标题行的垂直中心参照点
    Item {
        id: titleLineRef
        visible: false
        x: 0
        width: 0
        height: 0
        y: textCol.y + titleText.y + titleText.height / 2
    }

    Row {
        id: actionRow
        spacing: 6
        visible: children.length > 0
        // 操作按钮永远贴右边
        anchors.right: parent.right
        anchors.rightMargin: head.actionsRightMargin
        anchors.top: head.stacked ? textCol.bottom : undefined
        anchors.topMargin: head.stacked ? 8 : 0
        // 宽屏：actionsAlignToTitle 为真时按钮行与标题行垂直居中对齐 否则与整块页头居中对齐
        anchors.verticalCenter: head.stacked ? undefined  : (head.actionsAlignToTitle ? titleLineRef.verticalCenter   : parent.verticalCenter)
    }
}
