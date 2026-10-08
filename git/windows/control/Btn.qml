// 参考稿 .btn 及其变体 primary / secondary / danger（sm-btn、微型按钮用 small/tiny）
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    property string kind: "secondary"   // primary | secondary | danger | ghost
    property bool small: false
    property bool tiny: false
    // 可选图标（qrc:/img/xxx.svg）：留空就还是纯文字按钮，布局与原来完全一致
    property string iconSource: ""
    property int iconSize: control.tiny ? 12 : (control.small ? 14 : 16)

    implicitHeight: control.tiny ? 22 : (control.small ? 26 : 30)
    leftPadding: control.tiny ? 7 : (control.small ? 9 : 11)
    rightPadding: leftPadding
    font.pixelSize: control.tiny ? 10 : (control.small ? 11 : 12)
    font.weight: Font.DemiBold
    font.family: Theme.fontFamily

    background: Rectangle {
        radius: control.small || control.tiny ? 6 : 7
        color: {
            if (control.kind === "primary")
                return control.down ? Theme.accentPress : (control.hovered ? Theme.accentHover : Theme.accent)
            if (control.kind === "danger")
                return control.hovered ? Theme.dangerSoft : Theme.surface
            if (control.kind === "ghost")
                return control.hovered ? Theme.hover : "transparent"
            return control.hovered ? Theme.surfaceInset : Theme.surface
        }
        border.width: control.kind === "primary" || control.kind === "ghost" ? 0 : 1
        border.color: control.kind === "danger" ? Theme.dangerLine : Theme.line2
    }

    // 内容：图标（可选）+ 文字。整行居中，无图标时与原来的纯文字内容项等价
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: control.iconSource.length > 0 ? 5 : 0

            Image {
                visible: control.iconSource.length > 0
                source: control.iconSource
                width: visible ? control.iconSize : 0
                height: visible ? control.iconSize : 0
                fillMode: Image.PreserveAspectFit
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: control.text
                font: control.font
                color: control.kind === "primary" ? "#ffffff"
                    : (control.kind === "danger" ? Theme.danger : Theme.text2)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
