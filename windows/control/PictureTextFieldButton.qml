import QtQuick
import QtQuick.Controls

Item {
    id: root
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property string iconSource: ""
    property int iconSize: 16
    property int padding: 6
    property int itemWidth: 180
    property int itemHeight: 32
    property int fontSize: 12

    implicitWidth: itemWidth
    implicitHeight: itemHeight

    // 当前是否展开(选中)状态
    property bool active: false
    // 输入完成后按回车触发
    signal accepted()

    // 未展开时：整块可点击，点击后展开并聚焦输入框
    MouseArea {
        anchors.fill: parent
        enabled: !root.active
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.active = true
            input.forceActiveFocus()
        }
    }

    Image {
        id: icon
        source: root.iconSource
        width: root.iconSize
        height: root.iconSize
        anchors.verticalCenter: parent.verticalCenter
        x: root.active ? root.padding : (parent.width - width) / 2
        Behavior on x {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutQuad
            }
        }
    }

    TextField {
        id: input
        font.pixelSize: root.fontSize
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.active ? root.padding * 2 + root.iconSize : root.padding
        visible: root.active || text.length > 0
        opacity: root.active ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        background: Rectangle {
            color: root.active ? Qt.rgba(0.5, 0.5, 0.5, 0.07) : "transparent"
            radius: 4
            Behavior on color {
                ColorAnimation { duration: 150 }
            }
        }

        onActiveFocusChanged: {
            // 失焦且没有内容时收回为纯图标
            if (!activeFocus && text.length === 0)
            {
                root.active = false
            }
        }
        // 外部(如回填路径/类型名)写入文本时自动展开: 触发图标左移动画与输入框淡入
        onTextChanged: {
            if (text.length > 0)
            {
                root.active = true
            }
        }
        onAccepted: root.accepted()
    }
}
