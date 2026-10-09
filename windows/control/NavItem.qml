import QtQuick

Rectangle {
    id: item

    property string glyph: ""
    property string iconSource: ""
    property string text: ""
    property string count: ""
    property bool active: false
    property bool compact: false
    signal clicked()

    implicitHeight: 32
    implicitWidth: row.implicitWidth + (compact || count.length === 0 ? 20 : 40)
    radius: 7
    color: item.active ? Theme.accentSoft : (mouse.containsMouse ? Theme.hover : "transparent")

    Behavior on color {
        ColorAnimation { duration: 150; easing: Easing.OutCubic }
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: 9
        spacing: 8

        // 图标槽：固定 18×18，有 iconSource 就画 svg 否则退回 glyph 文字（两者占位一致 布局不变）
        Item {
            id: iconSlot
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter

            Image {
                anchors.fill: parent
                visible: item.iconSource.length > 0
                source: item.iconSource
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Text {
                anchors.fill: parent
                visible: item.iconSource.length === 0
                text: item.glyph
                font.pixelSize: Theme.px(12)
                font.family: Theme.fontFamily
                color: item.active ? Theme.accentText : Theme.text2
                opacity: 0.85
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            text: item.text
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            font.weight: item.active ? Font.DemiBold : Font.Normal
            color: item.active ? Theme.accentText : Theme.text2
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Text {
        visible: !item.compact && item.count.length > 0
        text: item.count
        font.pixelSize: Theme.px(10)
        font.family: Theme.fontFamily
        font.weight: Font.Medium
        color: item.active ? Theme.accentIcon : Theme.text3
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: item.clicked()
    }
}
