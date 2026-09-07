import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string tagColor: "#FFB6C1"
    property string borderColor: tagColor
    property string tagText: ""
    property int rectWidth: 64
    property int rectHeight: 24

    width: rectWidth
    height: rectHeight
    border.color: borderColor
    border.width: 1
    radius: Math.min(width, height) / 6

    // 初始位置（按下时记录）
    property point initialPosition: Qt.point(0, 0)
    // 当标签完全移出父容器时发射
    signal exitedParent()
    // 当前是否已完全位于父容器之外
    property bool _wasOutside: false
    // 拖拽时临时改挂的原父级/原层级（用于置顶且不被父容器裁剪）
    property Item _originalParent: null
    property int _originalZ: 0
    // 所属 TagContainer（按下时确定，拖拽中 parent 已临时改变，不能靠遍历父链查找）
    property Item _dragContainer: null

    ParallelAnimation {
        id: returnAnimation
        NumberAnimation {
            id: animX
            target: root
            property: "x"
            duration: 200
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            id: animY
            target: root
            property: "y"
            duration: 200
            easing.type: Easing.OutQuad
        }
    }

    Drag.active: dragArea.dragging
    Drag.keys: ["tag"]
    Drag.hotSpot.x: dragArea.pressOffset.x
    Drag.hotSpot.y: dragArea.pressOffset.y
    Drag.mimeData: {
        "text/plain": root.tagText,
        "text/color": root.tagColor
    }
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction

    MouseArea {
        id: dragArea
        anchors.fill: parent
        preventStealing: true
        propagateComposedEvents: false

        property point pressOffset: Qt.point(0, 0)
        property bool dragging: false

        onPressed: function(mouse)
        {
            returnAnimation.stop()

            root.initialPosition = Qt.point(root.x, root.y)
            root._dragContainer = findContainer()
            pressOffset = Qt.point(mouse.x, mouse.y)
            dragging = true

            var top = findTopItem()
            if (top)
            {
                root._originalParent = root.parent
                root._originalZ = root.z
                var pos = top.mapFromItem(root, 0, 0)
                root.parent = top
                root.x = pos.x
                root.y = pos.y
                root.z = 10000
            }
        }

        onPositionChanged: function(mouse)
        {
            if (dragging)
            {
                root.x = root.x + (mouse.x - pressOffset.x)
                root.y = root.y + (mouse.y - pressOffset.y)
                checkIfFullyOutside()
            }
        }

        onReleased: function(mouse) {
            root.Drag.drop()
            dragging = false

            if (root._originalParent)
            {
                root.parent = root._originalParent
                root.z = root._originalZ
                root._originalParent = null
            }

            if (root._wasOutside) {
                root.exitedParent()
                root._wasOutside = false
                root._dragContainer = null
                return
            }

            animX.to = root.initialPosition.x
            animY.to = root.initialPosition.y
            returnAnimation.start()

            root._wasOutside = false
            root._dragContainer = null
        }

        // 检查是否完全位于所属 TagContainer 之外
        function checkIfFullyOutside() {
            var container = root._dragContainer || findContainer()
            if (!container) return

            var topLeft = root.mapToItem(container, 0, 0)
            var tagRect = Qt.rect(topLeft.x, topLeft.y, root.width, root.height)
            var containerRect = Qt.rect(0, 0, container.width, container.height)

            root._wasOutside = !rectsIntersect(tagRect, containerRect)
        }

        function rectsIntersect(r1, r2) {
            return r1.x < r2.x + r2.width && r2.x < r1.x + r1.width &&
                   r1.y < r2.y + r2.height && r2.y < r1.y + r1.height
        }

        // 向上查找所属的 TagContainer（具备 containerName 属性的父级）
        function findContainer() {
            var obj = root.parent
            while (obj) {
                if (obj.hasOwnProperty("containerName")) return obj
                obj = obj.parent
            }
            return null
        }

        // 向上查找窗口顶层 Item（即窗口 contentItem）作为拖拽时的临时父级
        function findTopItem() {
            var obj = root.parent
            var top = null
            while (obj) {
                if (typeof obj.z !== "undefined") top = obj
                obj = obj.parent
            }
            return top
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            Layout.preferredHeight: 1
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height) * 0.9
                height: width
                color: root.tagColor
                radius: Math.min(width, height) / 4
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 7
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: root.tagText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 10
                color: "black"
                elide: Text.ElideRight
            }
        }
    }
}