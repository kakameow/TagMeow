import QtQuick

// 可拖拽标签小片

Rectangle {
    id: root

    // 标签文本 / 类型色
    property string tagText: ""
    property color tagColor: Theme.accent

    // 文字口径留给使用方微调 默认跟 Theme 走
    property int fontSize: Theme.px(11)
    property color textColor: Theme.text2

    // 拖出所属落点容器时发 没人接就只是弹回原位
    signal exitedParent()

    // 按下时记录的原位置 所在父级坐标系
    property point initialPosition: Qt.point(0, 0)
    // 当前是否已经完全位于所属容器之外
    property bool _wasOutside: false
    // 拖拽时临时改挂的原父级 / 原层级 用于置顶且不被父容器裁剪
    property Item _originalParent: null
    property int _originalZ: 0
    // 所属落点容器 按下时确定：拖拽中 parent 已临时改变 不能靠遍历父链现找
    property Item _dragContainer: null

    implicitWidth: contentRow.implicitWidth + 16
    implicitHeight: Math.max(22, contentRow.implicitHeight + 8)
    radius: 6
    color: Theme.surface
    border.width: 1
    border.color: (dragArea.containsMouse || dragArea.dragging) ? Theme.controlLine : Theme.borderFaint

    opacity: dragArea.dragging ? 0.92 : 1.0

    // 色点 + 标签名
    Row {
        id: contentRow

        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        Rectangle {
            width: 7
            height: 7
            radius: 3.5
            color: root.tagColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.tagText
            font.pixelSize: root.fontSize
            font.family: Theme.fontFamily
            color: root.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // 没被接收时滑回原位
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

    // 拖拽
    Drag.active: dragArea.dragging
    Drag.keys: ["tag"]
    Drag.hotSpot.x: dragArea.pressOffset.x
    Drag.hotSpot.y: dragArea.pressOffset.y
    Drag.mimeData: ({
        "text/plain": root.tagText,
        "text/color": String(root.tagColor)
    })
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction

    MouseArea {
        id: dragArea

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        propagateComposedEvents: false
        cursorShape: Qt.OpenHandCursor

        property point pressOffset: Qt.point(0, 0)
        property bool dragging: false

        onPressed: (mouse) => {
            returnAnimation.stop()

            root.initialPosition = Qt.point(root.x, root.y)
            root._dragContainer = findContainer()
            pressOffset = Qt.point(mouse.x, mouse.y)
            dragging = true

            // 临时改挂到窗口顶层 Item：拖拽中置顶且不被父容器裁剪
            var top = findTopItem()

            if (top) {
                root._originalParent = root.parent
                root._originalZ = root.z
                var pos = top.mapFromItem(root, 0, 0)
                root.parent = top
                root.x = pos.x
                root.y = pos.y
                root.z = 10000
            }
        }

        onPositionChanged: (mouse) => {
            if (!dragging)
                return

            root.x = root.x + (mouse.x - pressOffset.x)
            root.y = root.y + (mouse.y - pressOffset.y)
            checkIfFullyOutside()
        }

        onReleased: (mouse) => {
            // 显式投递：由光标下的 DropArea 处理
            root.Drag.drop()
            dragging = false

            if (root._originalParent) {
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

        // 检查是否已经完全位于所属落点容器之外
        function checkIfFullyOutside() {
            var container = root._dragContainer

            if (!container)
                container = findContainer()

            if (!container)
                return

            var topLeft = root.mapToItem(container, 0, 0)
            var tagRect = Qt.rect(topLeft.x, topLeft.y, root.width, root.height)
            var containerRect = Qt.rect(0, 0, container.width, container.height)

            root._wasOutside = !rectsIntersect(tagRect, containerRect)
        }

        function rectsIntersect(r1, r2) {
            return r1.x < r2.x + r2.width && r2.x < r1.x + r1.width &&
                   r1.y < r2.y + r2.height && r2.y < r1.y + r1.height
        }

        // 向上查找所属落点容器
        function findContainer() {
            var obj = root.parent

            while (obj) {
                if (obj.hasOwnProperty("containerName"))
                    return obj

                obj = obj.parent
            }

            return null
        }

        // 向上查找窗口顶层 Item z属性存在的最外层 作为拖拽时的临时父级
        function findTopItem() {
            var obj = root.parent
            var top = null

            while (obj) {
                if (typeof obj.z !== "undefined")
                    top = obj

                obj = obj.parent
            }

            return top
        }
    }

}
