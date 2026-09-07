import QtQuick
import QtQuick.Controls

Button {
    property string iconSource: ""
    property int buttonWidth: 32
    property int buttonHeight: 32
    property int iconWidth: 32
    property int iconHeight: 32

    width: buttonWidth
    height: buttonHeight
    flat: true

    background: Rectangle
    {
        color: {
            color: parent.hovered ? Qt.rgba(0, 0, 0, 0.1) : "transparent"
        }
        radius: 8
    }

    contentItem: Image {
        source: parent.iconSource
        width: parent.iconWidth
        height: parent.iconHeight
        anchors.centerIn: parent
        fillMode: Image.PreserveAspectFit
        opacity: parent.down ? 0.7 : 1.0
    }
}