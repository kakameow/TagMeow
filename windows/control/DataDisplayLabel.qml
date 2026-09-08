import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string iconSource: ""
    property string tipText: ""
    property string dataText: ""
    property string bcakgroundColor: "transparent"
    property int iconSize: 16
    property int itemWidth: 28
    property int itemHeight: 32
    property int fontSize: 12

    width: itemWidth * 3
    height: itemHeight
    radius: 12
    color: bcakgroundColor

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Image {
                anchors.centerIn: parent
                width: parent.parent.parent.iconSize
                height: parent.parent.parent.iconSize
                source: parent.parent.parent.iconSource
                fillMode: Image.PreserveAspectFit
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: parent.parent.parent.tipText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: root.fontSize
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: parent.parent.parent.dataText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: root.fontSize
                elide: Text.ElideRight
            }
        }
    }
}
