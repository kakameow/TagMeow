import QtQuick
import QtQuick.Controls

// 标签容器

Rectangle {
    id: container

    // 落点容器标记：TagRectangle 就是向上找这个属性来认所属容器的
    property string containerName: "tagContainer"

    // 头部：图标 + 组名
    property string iconSource: ""
    property string title: ""
    property int fontSize: Theme.px(10)

    // 本容器里的标签：[{ tag, color, index }]
    // index = 它在外部列表（页面的 Store.filters）里的真实下标 删的时候用
    property var tags: []

    // 空的时候显示的提示 留空 = 不显示
    property string placeholder: ""

    // 底色 / 描边由使用方按分组给
    property color groupColor: "transparent"
    property color groupLine: Theme.line2

    // 把标签拖进来了 / 里面的小片被拖出去了 参数是 tags 里的下标
    signal tagDropped(string tag)
    signal tagRemoved(int index)

    // 拖拽悬停中 使用方可以据此亮边框 容器自己也用
    readonly property bool dropActive: dropTarget.containsDrag

    radius: 8
    color: container.dropActive ? Theme.tintBlue : container.groupColor
    border.width: container.dropActive ? 2 : 1
    border.color: container.dropActive ? Theme.text : container.groupLine

    // 整块容器都是落点 声明在最前面 = 画在最底层 不挡上面的小片
    TagDropArea {
        id: dropTarget
        anchors.fill: parent
        onTagDropped: (tag) => container.tagDropped(tag)
    }

    Image {
        id: headIcon
        visible: container.iconSource.length > 0
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        source: container.iconSource
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Text {
        id: headLabel
        anchors.left: headIcon.visible ? headIcon.right : parent.left
        anchors.leftMargin: headIcon.visible ? 4 : 8
        anchors.verticalCenter: parent.verticalCenter
        text: container.title
        font.pixelSize: container.fontSize
        font.weight: Font.Bold
        font.family: Theme.fontFamily
        color: Theme.text2
    }

    // 小片区：横向排布 + 溢出横向滚动
    Flickable {
        id: chipsFlick
        anchors.left: headLabel.right
        anchors.leftMargin: 5
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        height: Math.max(20, Math.min(container.height - 8, 22))
        contentWidth: chipsRow.width
        contentHeight: chipsRow.height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        WheelHandler {
            onWheel: (event) => {
                var maxX = Math.max(0, chipsFlick.contentWidth - chipsFlick.width)
                if (maxX > 0) {
                    var delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                    chipsFlick.contentX = Math.max(0, Math.min(maxX, chipsFlick.contentX + delta))
                    event.accepted = true
                }
            }
        }

        ScrollBar.horizontal: ScrollBar {
            id: chipsBar
            policy: ScrollBar.AlwaysOff
            height: 3
            padding: 0
            background: Item { }

            contentItem: Rectangle {
                implicitHeight: 3
                radius: 1.5
                color: chipsBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
            }
        }

        Row {
            id: chipsRow
            width: childrenRect.width
            height: chipsFlick.height
            spacing: 5

            Repeater {
                model: container.tags

                delegate: TagRectangle {
                    required property var modelData

                    tagText: modelData.tag
                    tagColor: modelData.color
                    onExitedParent: container.tagRemoved(modelData.index)
                }
            }
        }
    }

    // 空态提示
    Text {
        anchors.left: headLabel.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: container.tags.length === 0 && container.placeholder.length > 0
        text: container.placeholder
        font.pixelSize: container.fontSize
        font.family: Theme.fontFamily
        color: Theme.text3
    }
}
