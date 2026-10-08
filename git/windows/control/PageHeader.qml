// 参考稿 .page-head（eyebrow / h1 / page-desc + 右侧操作按钮）
// 用法：PageHeader { eyebrow: "..."; title: "..."; desc: "..."
//                    Btn { text: "添加目录"; kind: "primary" } }   ← 默认子项会进右侧按钮行
// 右侧按钮行永远贴右边；垂直位置：默认与整块页头垂直居中，
// actionsAlignToTitle: true 时与标题行（h1）垂直居中（目录页用）
import QtQuick

Item {
    id: head

    property string eyebrow: ""
    property string title: ""
    property string desc: ""
    property bool stacked: false      // 窄屏（<760）时纵向排布
    property int actionsRightMargin: 0 // 右侧操作区留白（与下方卡片内边距对齐用）
    // 右侧操作按钮是否与标题行（h1）垂直居中对齐；默认 false = 与整块页头垂直居中（原行为）
    property bool actionsAlignToTitle: false
    default property alias actions: actionRow.data

    implicitHeight: stacked ? (textCol.height + 8 + (actionRow.visible ? actionRow.height : 0))
                            : Math.max(textCol.height, actionRow.visible ? 30 : 0)
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

    // 标题行（h1）的垂直中心参照点：QML 只允许锚到父项或同级项，
    // 所以放一个与 actionRow 同级的 0×0 不可见项，y 绑到 titleText 的中心
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
        // 操作按钮永远贴右边（窄屏纵向排布时也不改到左边，只有上下位置变）
        anchors.right: parent.right
        anchors.rightMargin: head.actionsRightMargin
        anchors.top: head.stacked ? textCol.bottom : undefined
        anchors.topMargin: head.stacked ? 8 : 0
        // 宽屏：actionsAlignToTitle 为真时按钮行与标题行（h1）垂直居中对齐，否则与整块页头居中对齐
        anchors.verticalCenter: head.stacked ? undefined
                                              : (head.actionsAlignToTitle ? titleLineRef.verticalCenter
                                                                          : parent.verticalCenter)
    }
}
