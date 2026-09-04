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

    // 初始位置
    property point initialPosition: Qt.point(0, 0)
    // 当标签完全移出父容器时发射
    signal exitedParent()
    // 当前是否已完全位于父容器之外
    property bool _wasOutside: false
    // 拖拽时临时改挂的原父级/原层级（用于置顶且不被父容器裁剪）
    property Item _originalParent: null
    property int _originalZ: 0
    // 所属 TagContainer（按下时确定 拖拽中 parent 已临时改变 不能靠遍历父链查找）
    property Item _dragContainer: null

    Drag.active: dragArea.dragging
    Drag.keys: ["tag"]
    Drag.hotSpot.x: dragArea.pressOffset.x
    Drag.hotSpot.y: dragArea.pressOffset.y
    Drag.mimeData:
    {
        "text/plain": root.tagText,
        "text/color": root.tagColor
    }
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction

    MouseArea {
        id: dragArea
        anchors.fill: parent
        // 防止 GridView/Flickable 抢走鼠标导致页面滚动
        preventStealing: true
        // 拖拽时捕获鼠标 防止被内部控件拦截
        propagateComposedEvents: false

        // 记录按下时的鼠标偏移
        property point pressOffset: Qt.point(0, 0)
        // 是否正在拖拽
        property bool dragging: false

        onPressed: function(mouse)
        {
            root.initialPosition = Qt.point(root.x, root.y)
            root._dragContainer = findContainer()
            pressOffset = Qt.point(mouse.x, mouse.y)
            dragging = true

            // 置顶：临时改挂到窗口顶层 Item（无裁剪） 避免被其它元素覆盖/被父容器裁剪
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
                // 移动标签（跟随鼠标 相对于父容器）
                root.x = root.x + (mouse.x - pressOffset.x)
                root.y = root.y + (mouse.y - pressOffset.y)

                checkIfFullyOutside()
            }
        }

        onReleased: function(mouse)
        {
            // 先交付 drop 事件：若拖到了其它容器的 DropArea 上 由对方复制标签内容
            root.Drag.drop()
            // 结束拖拽
            dragging = false

            // 恢复原父级与层级
            if (root._originalParent)
            {
                root.parent = root._originalParent
                root.z = root._originalZ
                root._originalParent = null
            }

            // 在松开时发射 松开时若已完全移出父容器 则发送信号 由 父元素处理
            if (root._wasOutside)
            {
                root.exitedParent()
            }
            root._wasOutside = false
            root._dragContainer = null

            root.x = root.initialPosition.x
            root.y = root.initialPosition.y
        }

        // 检查是否完全位于所属 TagContainer 之外
        function checkIfFullyOutside()
        {
            var container = root._dragContainer || findContainer()
            if (!container)
            {
                return
            }

            // 将标签矩形换算到容器坐标系（mapToItem 已包含滚动/布局偏移）
            var topLeft = root.mapToItem(container, 0, 0)
            var tagRect = Qt.rect(topLeft.x, topLeft.y, root.width, root.height)
            var containerRect = Qt.rect(0, 0, container.width, container.height)

            root._wasOutside = !rectsIntersect(tagRect, containerRect)
        }

        function rectsIntersect(r1, r2)
        {
            return r1.x < r2.x + r2.width && r2.x < r1.x + r1.width && r1.y < r2.y + r2.height && r2.y < r1.y + r1.height
        }

        // 向上查找所属的 TagContainer（具备 containerName 属性的父级）
        function findContainer()
        {
            var obj = root.parent
            while (obj)
            {
                if (obj.hasOwnProperty("containerName"))
                {
                    return obj
                }
                obj = obj.parent
            }
            return null
        }

        // 向上查找窗口顶层 Item（即窗口 contentItem） 作为拖拽时的临时父级
        // 注意：不能用 contentItem 属性判定——GridView/Flickable 也有该属性
        // 用 z 属性区分 Item 与 Window（Window 没有 z）
        function findTopItem()
        {
            var obj = root.parent
            var top = null
            while (obj)
            {
                if (typeof obj.z !== "undefined")
                {
                    top = obj
                }
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
