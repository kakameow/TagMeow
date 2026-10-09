import QtQuick

Rectangle {
    id: chip

    property string text: ""
    property color dotColor: Theme.accent
    property bool removable: true
    signal removed()

    implicitHeight: 22
    implicitWidth: row.implicitWidth + 16
    radius: 6
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderFaint

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5

        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: chip.dotColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: chip.text
            font.pixelSize: Theme.px(11)
            font.family: Theme.fontFamily
            color: Theme.textDeep
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: chip.removable
            text: "✕"
            font.pixelSize: Theme.px(9)
            font.family: Theme.fontFamily
            color: Theme.text3
            anchors.verticalCenter: parent.verticalCenter

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                onClicked: chip.removed()
            }
        }
    }
}
