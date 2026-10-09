import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 标签库页面

Item {
    id: page
    readonly property bool oneColumn: width < 980

    //  固定尺寸比例（窗口锁定 1280×820 内容区可用高度 740：页面被填满 不整页滚动）
    readonly property int contentH: 740
    readonly property int cardH: 292

    readonly property int displayH: oneColumn ? Math.max(0, contentH - headerBox.height - (cardH * 2 + 10) - 20) : contentH - headerBox.height - 10 - cardH - 10

    property int pickR: 0
    property int pickG: 0
    property int pickB: 0

    // 危险操作确认：页级只放一个弹窗 待执行的动作与目标 id 存在 pendingAction / pendingId 里
    property string pendingAction: ""
    property int pendingId: -1
    property string pendingOldName: ""
    property string pendingName: ""

    // "#abc" / "#AABBCC" → { r, g, b } 非法值回退到 Store.typeColor（仍非法才用黑色兜底）
    function hexToRgb(hex) {
        var h = String(hex === undefined || hex === null ? "" : hex).trim()
        if (h.charAt(0) === "#")
            h = h.substring(1)
        if (h.length === 3)
            h = h.charAt(0) + h.charAt(0) + h.charAt(1) + h.charAt(1) + h.charAt(2) + h.charAt(2)
        if (!/^[0-9a-fA-F]{6}$/.test(h)) {
            var fallback = String(Store.typeColor === undefined || Store.typeColor === null ? "" : Store.typeColor)
            if (fallback.length > 0 && fallback !== String(hex))
                return page.hexToRgb(fallback)
            return { r: 0, g: 0, b: 0 }
        }
        return {
            r: parseInt(h.substring(0, 2), 16),
            g: parseInt(h.substring(2, 4), 16),
            b: parseInt(h.substring(4, 6), 16)
        }
    }

    // 0-255 取整
    function clamp255(n) {
        var v = Math.round(Number(n))
        if (isNaN(v))
            v = 0
        return Math.max(0, Math.min(255, v))
    }

    // 单个通道 两位大写十六进制
    function toHex2(n) {
        var s = page.clamp255(n).toString(16).toUpperCase()
        return s.length < 2 ? "0" + s : s
    }

    // r / g / b → "#RRGGBB"
    function rgbToHex(r, g, b) {
        return "#" + page.toHex2(r) + page.toHex2(g) + page.toHex2(b)
    }


    // 这两个是**页面内的查询函数**，不是桥的 invokable —— 它们读 ConfigBridge.tags / ConfigBridge.types
    function tagsOfType(typeId) {
        var out = []
        for (var i = 0; i < ConfigBridge.tags.length; i++) {
            if (ConfigBridge.tags[i].typeId === typeId)
                out.push(ConfigBridge.tags[i])
        }
        return out
    }

    function typeColorOf(typeId) {
        for (var i = 0; i < ConfigBridge.types.length; i++) {
            if (ConfigBridge.types[i].typeId === typeId) {
                var c = ConfigBridge.types[i].color
                // core 理论上不会给空颜色，兜一层免得 color 绑定报 "Unable to assign [undefined]"
                return (c === undefined || c === null || c === "") ? Theme.accent : c
            }
        }
        return Theme.accent
    }

    // 打开选色盘
    function openColorPicker() {
        var rgb = page.hexToRgb(Store.typeColor)
        page.setPickerRgb(rgb.r, rgb.g, rgb.b)
        colorPopup.open()
    }

    // 把 RGB 同步到三个滑块 + HEX 输入框
    function setPickerRgb(r, g, b) {
        page.pickR = page.clamp255(r)
        page.pickG = page.clamp255(g)
        page.pickB = page.clamp255(b)
        rSlider.value = page.pickR
        gSlider.value = page.pickG
        bSlider.value = page.pickB
        hexInput.text = page.rgbToHex(page.pickR, page.pickG, page.pickB)
    }

    // 回填 HEX 输入框的规范值 输入框正在编辑时不打断（参考稿 refreshPicker 的 activeElement 判断）
    function syncHexField() {
        if (!hexInput.activeFocus)
            hexInput.text = page.rgbToHex(page.pickR, page.pickG, page.pickB)
    }

    // HEX 手输：合法 6 位十六进制立刻同步滑块与预览 非法输入忽略 等失焦回填
    function applyHexText() {
        var v = hexInput.text.trim()
        if (v.charAt(0) !== "#")
            v = "#" + v
        if (!/^#[0-9a-fA-F]{6}$/.test(v))
            return
        var rgb = page.hexToRgb(v)
        page.pickR = rgb.r
        page.pickG = rgb.g
        page.pickB = rgb.b
        rSlider.value = rgb.r
        gSlider.value = rgb.g
        bSlider.value = rgb.b
    }

    // 确定：规范化为大写 "#RRGGBB" 写入 Store.typeColor
    function confirmColorPicker() {
        var hex = page.rgbToHex(page.pickR, page.pickG, page.pickB)
        Store.typeColor = hex
        if (Store.selectedTypeId > 0) {
            ConfigBridge.setTypeColor(Store.selectedTypeId, hex)      // 成功时由 Store 提示"已更新类型颜色"
        } else {
            Store.toast(Lang.t("editor.color_updated_prefix") + hex)
        }
        colorPopup.close()
    }

    // 页面根节点固定为内容区可用高度740 内部各块定高因
    implicitHeight: page.contentH

    Column {
        id: col
        width: parent.width
        spacing: 10

        // 页头
        PageHeader {
            id: headerBox
            width: col.width
            eyebrow: Lang.t("tag.eyebrow")
            title: Lang.t("tag.library_title")
            desc: Lang.t("tag.desc")
        }

        GridLayout {
            id: grid
            width: col.width
            columns: page.oneColumn ? 1 : 2
            columnSpacing: 10
            rowSpacing: 10

            // 类型管理卡片
            Rectangle {
                id: typeCard
                implicitWidth: (grid.width - 10) / 2
                implicitHeight: page.cardH
                Layout.fillWidth: true
                Layout.fillHeight: !page.oneColumn
                radius: 10
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: typeCol
                    x: 12
                    y: 12
                    width: parent.width - 24
                    spacing: 9

                    Text {
                        text: Lang.t("tag.types_title")
                        height: 17
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    Rectangle {
                        id: typeInputRow
                        width: parent.width
                        height: typeInputFlow.height + 16
                        radius: 8
                        color: Theme.surfaceInset
                        border.width: 1
                        border.color: Theme.lineSoft

                        Flow {
                            id: typeInputFlow
                            x: 8
                            y: 8
                            width: parent.width - 16
                            spacing: 6

                            Text {
                                text: Lang.t("editor.color_label")
                                width: 22
                                height: 28
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(11)
                                font.weight: Font.DemiBold
                                font.family: Theme.fontFamily
                                color: Theme.text2
                            }

                            Rectangle {
                                id: typeColorBlock
                                width: 28
                                height: 28
                                radius: 7
                                color: "transparent"
                                border.width: 1
                                border.color: typeColorMouse.containsMouse ? Theme.accent : Theme.line2

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    radius: 6
                                    color: Store.typeColor
                                    border.width: 2
                                    border.color: "#ffffff"
                                }

                                MouseArea {
                                    id: typeColorMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.openColorPicker()
                                }
                            }

                            TextField {
                                id: typeNameInput
                                width: 120
                                height: 28
                                leftPadding: 8
                                rightPadding: 8
                                placeholderText: Lang.t("editor.type_name_label")
                                color: Theme.text
                                font.pixelSize: Theme.px(12)
                                font.family: Theme.fontFamily
                                verticalAlignment: TextInput.AlignVCenter
                                selectByMouse: true

                                background: Rectangle {
                                    radius: 6
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: typeNameInput.activeFocus ? Theme.accent : Theme.line2
                                }
                            }

                            Btn {
                                text: Lang.t("editor.add")
                                kind: "primary"
                                small: true
                                onClicked: {
                                    ConfigBridge.addType(typeNameInput.text, Store.typeColor)
                                    typeNameInput.text = ""
                                }
                            }

                            Btn {
                                text: Lang.t("editor.delete")
                                kind: "danger"
                                small: true
                                onClicked: {
                                    if (Store.selectedTypeId <= 0) {
                                        Store.toast(Lang.t("tag.pick_type_first"))
                                        return
                                    }
                                    page.pendingAction = "removeType"
                                    page.pendingId = Store.selectedTypeId
                                    confirmDialog.open()
                                }
                            }

                            TextField {
                                id: typeRenameInput
                                width: 110
                                height: 28
                                leftPadding: 8
                                rightPadding: 8
                                placeholderText: Lang.t("tag.rename_placeholder")
                                color: Theme.text
                                font.pixelSize: Theme.px(12)
                                font.family: Theme.fontFamily
                                verticalAlignment: TextInput.AlignVCenter
                                selectByMouse: true

                                background: Rectangle {
                                    radius: 6
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: typeRenameInput.activeFocus ? Theme.accent : Theme.line2
                                }
                            }

                            Btn {
                                text: Lang.t("common.rename")
                                small: true
                                onClicked: {
                                    // 语义：把**类型名称输入框**里的那个类型 重命名成**重命名输入框**里的名字
                                    var oldName = typeNameInput.text.trim()
                                    var newName = typeRenameInput.text.trim()
                                    if (oldName.length === 0) {
                                        Store.toast(Lang.t("tag.pick_type_first"))
                                        return
                                    }
                                    if (newName.length === 0) {
                                        Store.toast(Lang.t("editor.name_required"))
                                        return
                                    }
                                    page.pendingAction = "renameType"
                                    page.pendingId = -1
                                    page.pendingOldName = oldName
                                    page.pendingName = newName
                                    confirmDialog.open()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: typeTableBox
                        width: parent.width
                        height: 189
                        radius: 8
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.lineSoft
                        clip: true

                        Flickable {
                            id: typeFlick
                            anchors.fill: parent
                            contentWidth: width
                            contentHeight: typeTableCol.height
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.vertical: ScrollBar {
                                id: typeVBar
                                policy: ScrollBar.AlwaysOff
                                width: 7
                                background: Item { }
                                contentItem: Rectangle {
                                    implicitWidth: 7
                                    radius: 3.5
                                    color: typeVBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                }
                            }

                            Column {
                                id: typeTableCol
                                width: typeFlick.width
                                spacing: 0

                                // 表头：颜色 / 名称 / 标签数 / 操作（列宽 40 / 自适应 / 56 / 60）
                                Item {
                                    id: typeHead
                                    width: parent.width
                                    height: 27

                                    readonly property real nameColW: Math.max(60, typeHead.width - 156)

                                    Rectangle {
                                        anchors.fill: parent
                                        color: Theme.surface2
                                    }

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: Theme.lineSoft
                                    }

                                    Text {
                                        x: 9
                                        y: 0
                                        width: 31
                                        height: parent.height
                                        text: Lang.t("editor.color_label")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }

                                    Text {
                                        x: 49
                                        y: 0
                                        width: typeHead.nameColW - 18
                                        height: parent.height
                                        text: Lang.t("tag.col_name")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }

                                    Text {
                                        x: typeHead.width - 107
                                        y: 0
                                        width: 38
                                        height: parent.height
                                        text: Lang.t("tag.col_tag_count")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }

                                    Text {
                                        x: typeHead.width - 51
                                        y: 0
                                        width: 42
                                        height: parent.height
                                        text: Lang.t("tag.col_actions")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }
                                }

                                // 类型行：model 绑 ConfigBridge.types.length 用 ConfigBridge.types[index] 取值保证实时刷新
                                Repeater {
                                    model: ConfigBridge.types.length

                                    delegate: Item {
                                        id: typeRow
                                        required property int index

                                        readonly property var typeItem: ConfigBridge.types[index]
                                        readonly property bool selected: Store.selectedTypeId === typeRow.typeItem.typeId
                                        readonly property int tagTotal: page.tagsOfType(typeRow.typeItem.typeId).length
                                        readonly property real nameColW: Math.max(60, typeRow.width - 156)

                                        width: parent.width
                                        height: 27

                                        // 选中行
                                        Rectangle {
                                            anchors.fill: parent
                                            color: typeRow.selected ? Theme.accentSoft
                                                                   : (typeRowMouse.containsMouse ? Theme.surface2 : "transparent")
                                        }

                                        Rectangle {
                                            visible: typeRow.index < ConfigBridge.types.length - 1
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: Theme.lineSoft
                                        }

                                        // 行点击选中（在「删除」按钮下层 按钮点击不会传到这里）
                                        MouseArea {
                                            id: typeRowMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                Store.selectedTypeId = typeRow.typeItem.typeId
                                                Store.typeColor = page.typeColorOf(typeRow.typeItem.typeId)
                                                typeNameInput.text = typeRow.typeItem.name
                                            }
                                        }

                                        // .color-dot：12×12
                                        Rectangle {
                                            x: 9
                                            width: 12
                                            height: 12
                                            radius: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: page.typeColorOf(typeRow.typeItem.typeId)
                                            border.width: 1
                                            border.color: "#0f000000"
                                        }

                                        Text {
                                            x: 49
                                            y: 0
                                            width: typeRow.nameColW - 18
                                            height: parent.height
                                            text: typeRow.typeItem.name
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: Theme.px(12)
                                            font.weight: typeRow.selected ? 650 : Font.Normal
                                            font.family: Theme.fontFamily
                                            color: Theme.text
                                        }

                                        Text {
                                            x: typeRow.width - 107
                                            y: 0
                                            width: 38
                                            height: parent.height
                                            text: typeRow.tagTotal
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: Theme.px(12)
                                            font.family: Theme.fontFamily
                                            color: Theme.text
                                        }

                                        Btn {
                                            x: typeRow.width - 51
                                            width: 38
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: Lang.t("editor.delete")
                                            kind: "danger"
                                            tiny: true
                                            onClicked: {
                                                page.pendingAction = "removeType"
                                                page.pendingId = typeRow.typeItem.typeId
                                                confirmDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            // 空态：core 还没给出类型数据时表格区域显示灰字提示（有数据时被行盖住）
                            Text {
                                x: Math.max(0, (typeFlick.width - width) / 2)
                                y: Math.max(0, (typeFlick.height - height) / 2)
                                width: Math.min(implicitWidth, typeFlick.width - 24)
                                visible: ConfigBridge.types.length === 0
                                text: Lang.t("editor.empty_types")
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.px(11)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }
                        }
                    }
                }
            }

            // 标签管理卡片
            Rectangle {
                id: tagCard
                implicitWidth: (grid.width - 10) / 2
                implicitHeight: page.cardH
                Layout.fillWidth: true
                Layout.fillHeight: !page.oneColumn
                radius: 10
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: tagCol
                    x: 12
                    y: 12
                    width: parent.width - 24
                    spacing: 9

                    Text {
                        text: Lang.t("tag.tags_title")
                        height: 17
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    Rectangle {
                        id: tagInputRow
                        width: parent.width
                        height: tagInputFlow.height + 16
                        radius: 8
                        color: Theme.surfaceInset
                        border.width: 1
                        border.color: Theme.lineSoft

                        Flow {
                            id: tagInputFlow
                            x: 8
                            y: 8
                            width: parent.width - 16
                            spacing: 6

                            Text {
                                text: Lang.t("editor.group_label")
                                width: 44
                                height: 28
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.px(11)
                                font.weight: Font.DemiBold
                                font.family: Theme.fontFamily
                                color: Theme.text2
                            }

                            ComboBox {
                                id: tagTypeCombo
                                width: 120
                                height: 28
                                model: ConfigBridge.types
                                textRole: "name"
                                leftPadding: 8
                                rightPadding: 20
                                topPadding: 0
                                bottomPadding: 0
                                font.pixelSize: Theme.px(12)
                                font.family: Theme.fontFamily

                                background: Rectangle {
                                    radius: 6
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: tagTypeCombo.activeFocus ? Theme.accent : Theme.line2
                                }

                                contentItem: Text {
                                    leftPadding: 8
                                    rightPadding: 20
                                    text: tagTypeCombo.displayText
                                    font: tagTypeCombo.font
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }

                                indicator: Text {
                                    x: tagTypeCombo.width - 17
                                    y: 0
                                    width: 10
                                    height: tagTypeCombo.height
                                    text: "▾"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: Theme.px(9)
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                }

                                delegate: ItemDelegate {
                                    id: typeOption
                                    required property var model
                                    required property int index
                                    width: ListView.view.width
                                    height: 26

                                    background: Rectangle {
                                        color: typeOption.highlighted ? Theme.accentSoft : "transparent"
                                    }

                                    contentItem: Text {
                                        leftPadding: 8
                                        rightPadding: 8
                                        text: typeOption.model ? typeOption.model.name : ""
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(12)
                                        font.family: Theme.fontFamily
                                        color: Theme.text
                                    }
                                }

                                // 选中的类型被删掉时退回第一个类型
                                function syncIndex() {
                                    for (var i = 0; i < ConfigBridge.types.length; i++) {
                                        if (ConfigBridge.types[i].typeId === Store.selectedTypeId) {
                                            tagTypeCombo.currentIndex = i
                                            return
                                        }
                                    }
                                    if (ConfigBridge.types.length > 0) {
                                        tagTypeCombo.currentIndex = 0
                                        Store.selectedTypeId = ConfigBridge.types[0].typeId
                                    } else {
                                        tagTypeCombo.currentIndex = -1
                                    }
                                }

                                Component.onCompleted: tagTypeCombo.syncIndex()

                                onActivated: function (index) {
                                    if (index >= 0 && index < ConfigBridge.types.length)
                                        Store.selectedTypeId = ConfigBridge.types[index].typeId
                                }

                                Connections {
                                    // 选中项在 Store 纯 UI 态
                                    target: Store
                                    function onSelectedTypeIdChanged() { tagTypeCombo.syncIndex() }
                                }

                                Connections {
                                    // 类型列表在桥 增删改之后桥会发 typesChanged
                                    target: ConfigBridge
                                    function onTypesChanged() { tagTypeCombo.syncIndex() }
                                }
                            }

                            TextField {
                                id: tagNameInput
                                width: 120
                                height: 28
                                leftPadding: 8
                                rightPadding: 8
                                placeholderText: Lang.t("editor.tag_name_label")
                                color: Theme.text
                                font.pixelSize: Theme.px(12)
                                font.family: Theme.fontFamily
                                verticalAlignment: TextInput.AlignVCenter
                                selectByMouse: true

                                background: Rectangle {
                                    radius: 6
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: tagNameInput.activeFocus ? Theme.accent : Theme.line2
                                }
                            }

                            Btn {
                                text: Lang.t("editor.add")
                                kind: "primary"
                                small: true
                                onClicked: {
                                    ConfigBridge.addTag(Store.selectedTypeId, tagNameInput.text)
                                    tagNameInput.text = ""
                                }
                            }

                            Btn {
                                text: Lang.t("editor.delete")
                                kind: "danger"
                                small: true
                                onClicked: {
                                    if (Store.selectedTagId <= 0) {
                                        Store.toast(Lang.t("tag.pick_tag_first"))
                                        return
                                    }
                                    page.pendingAction = "removeTag"
                                    page.pendingId = Store.selectedTagId
                                    confirmDialog.open()
                                }
                            }
                        }
                    }

                    // .table-scroll：当前类型下的标签
                    Rectangle {
                        id: tagTableBox
                        width: parent.width
                        height: 189
                        radius: 8
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.lineSoft
                        clip: true

                        Flickable {
                            id: tagFlick
                            anchors.fill: parent
                            contentWidth: width
                            contentHeight: tagTableCol.height
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.vertical: ScrollBar {
                                id: tagVBar
                                policy: ScrollBar.AlwaysOff
                                width: 7
                                background: Item { }
                                contentItem: Rectangle {
                                    implicitWidth: 7
                                    radius: 3.5
                                    color: tagVBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                                }
                            }

                            Column {
                                id: tagTableCol
                                width: tagFlick.width
                                spacing: 0

                                // 表头：颜色 / 标签名 / 操作（列宽 40 / 自适应 / 60）
                                Item {
                                    id: tagHead
                                    width: parent.width
                                    height: 27

                                    readonly property real nameColW: Math.max(60, tagHead.width - 100)

                                    Rectangle {
                                        anchors.fill: parent
                                        color: Theme.surface2
                                    }

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: Theme.lineSoft
                                    }

                                    Text {
                                        x: 9
                                        y: 0
                                        width: 31
                                        height: parent.height
                                        text: Lang.t("editor.color_label")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }

                                    Text {
                                        x: 49
                                        y: 0
                                        width: tagHead.nameColW - 18
                                        height: parent.height
                                        text: Lang.t("editor.tag_name_label")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }

                                    Text {
                                        x: tagHead.width - 51
                                        y: 0
                                        width: 42
                                        height: parent.height
                                        text: Lang.t("tag.col_actions")
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(10)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.family: Theme.fontFamily
                                        color: Theme.text2
                                    }
                                }

                                // 标签行：只列当前选中类型的标签
                                Repeater {
                                    model: page.tagsOfType(Store.selectedTypeId).length

                                    delegate: Item {
                                        id: tagRow
                                        required property int index

                                        readonly property var tagItem: page.tagsOfType(Store.selectedTypeId)[index]
                                        readonly property bool selected: Store.selectedTagId === tagRow.tagItem.tagId
                                        readonly property real nameColW: Math.max(60, tagRow.width - 100)

                                        width: parent.width
                                        height: 27

                                        Rectangle {
                                            anchors.fill: parent
                                            // 选中行 #eef1ff（Theme.accentSoft）/ hover #fafbfc（Theme.surface2），选中优先
                                            color: tagRow.selected ? Theme.accentSoft
                                                                  : (tagRowMouse.containsMouse ? Theme.surface2 : "transparent")
                                        }

                                        Rectangle {
                                            visible: tagRow.index < page.tagsOfType(Store.selectedTypeId).length - 1
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: Theme.lineSoft
                                        }

                                        MouseArea {
                                            id: tagRowMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: Store.selectedTagId = tagRow.tagItem.tagId
                                        }

                                        // 色点用所属类型的颜色
                                        Rectangle {
                                            x: 9
                                            width: 12
                                            height: 12
                                            radius: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: page.typeColorOf(tagRow.tagItem.typeId)
                                            border.width: 1
                                            border.color: "#0f000000"
                                        }

                                        Text {
                                            x: 49
                                            y: 0
                                            width: tagRow.nameColW - 18
                                            height: parent.height
                                            text: tagRow.tagItem.name
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: Theme.px(12)
                                            font.weight: tagRow.selected ? 650 : Font.Normal
                                            font.family: Theme.fontFamily
                                            color: Theme.text
                                        }

                                        Btn {
                                            x: tagRow.width - 51
                                            width: 38
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: Lang.t("editor.delete")
                                            kind: "danger"
                                            tiny: true
                                            onClicked: {
                                                page.pendingAction = "removeTag"
                                                page.pendingId = tagRow.tagItem.tagId
                                                confirmDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            // 空态：当前类型下没有标签时表格区域显示灰字提示
                            Text {
                                x: Math.max(0, (tagFlick.width - width) / 2)
                                y: Math.max(0, (tagFlick.height - height) / 2)
                                width: Math.min(implicitWidth, tagFlick.width - 24)
                                visible: page.tagsOfType(Store.selectedTypeId).length === 0
                                text: Lang.t("editor.empty_tags")
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.px(11)
                                font.family: Theme.fontFamily
                                color: Theme.text3
                            }
                        }
                    }
                }
            }
        }

        // 整页标签预览
        Flickable {
            id: displayFlick
            width: col.width
            height: page.displayH
            contentWidth: width
            contentHeight: displayCol.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: ScrollBar {
                id: displayVBar
                policy: ScrollBar.AlwaysOff
                width: 7
                background: Item { }
                contentItem: Rectangle {
                    implicitWidth: 7
                    radius: 3.5
                    color: displayVBar.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
                }
            }

            Column {
                id: displayCol
                width: displayFlick.width
                spacing: 8

                Repeater {
                    model: ConfigBridge.types.length

                    delegate: Rectangle {
                        id: groupCard
                        required property int index

                        readonly property var typeItem: ConfigBridge.types[index]
                        readonly property var groupTags: page.tagsOfType(groupCard.typeItem.typeId)
                        readonly property var pillItems: {
                            var out = []
                            for (var i = 0; i < groupCard.groupTags.length; i++)
                                out.push({ name: groupCard.groupTags[i].name, color: groupCard.typeItem.color })
                            return out
                        }

                        width: parent.width
                        height: groupCol.height + 18
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.line

                        Column {
                            id: groupCol
                            x: 10
                            y: 9
                            width: parent.width - 20
                            spacing: 7

                            Item {
                                id: groupHead
                                width: parent.width
                                height: 16

                                Row {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6

                                    Rectangle {
                                        width: 11
                                        height: 11
                                        radius: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: groupCard.typeItem.color
                                    }

                                    Text {
                                        text: groupCard.typeItem.name
                                        height: 16
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: Theme.px(12)
                                        font.weight: Font.DemiBold
                                        font.family: Theme.fontFamily
                                        color: Theme.text
                                    }
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: groupCard.groupTags.length + Lang.t("common.count_suffix")
                                    font.pixelSize: Theme.px(10)
                                    font.family: Theme.fontFamily
                                    color: Theme.text3
                                }
                            }

                            Flow {
                                width: parent.width
                                spacing: 5

                                Repeater {
                                    model: groupCard.pillItems

                                    delegate: Item {
                                        id: pill
                                        required property var modelData

                                        width: pillBox.width
                                        height: 25

                                        Rectangle {
                                            id: pillBox
                                            width: pillRow.implicitWidth + 18
                                            height: 24
                                            y: pillMouse.containsMouse ? 0 : 1
                                            radius: 6
                                            color: Theme.surface
                                            border.width: 1
                                            border.color: pillMouse.containsMouse ? Theme.controlLine : Theme.line2

                                            Row {
                                                id: pillRow
                                                anchors.centerIn: parent
                                                spacing: 5

                                                Rectangle {
                                                    width: 7
                                                    height: 7
                                                    radius: 3.5
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: pill.modelData.color
                                                }

                                                Text {
                                                    text: pill.modelData.name
                                                    height: 16
                                                    verticalAlignment: Text.AlignVCenter
                                                    font.pixelSize: Theme.px(11)
                                                    font.family: Theme.fontFamily
                                                    color: Theme.text2
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }

                                            MouseArea {
                                                id: pillMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Store.toast(Lang.t("tip.tag") + " " + pill.modelData.name)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                x: Math.max(0, (displayFlick.width - width) / 2)
                y: Math.max(0, (displayFlick.height - height) / 2)
                visible: ConfigBridge.types.length === 0
                text: Lang.t("tag.library_empty_hint")
                font.pixelSize: Theme.px(11)
                font.family: Theme.fontFamily
                color: Theme.text3
            }
        }
    }

    // 危险操作确认弹窗（删除类型 / 删除标签 / 重命名类型共用）
    // 行内「删除」按钮与编辑器里的「删除」按钮都只写 pendingAction / pendingId 再 open()
    // Repeater 里不会一行一个 Dialog「重命名」还额外把新名称写进 pendingName
    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        width: Math.min(420, page.width - 40)
        modal: true
        title: Lang.t("confirm.title")

        background: Rectangle {
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
        }

        contentItem: Text {
            text: page.pendingAction === "removeTag" ? Lang.t("confirm.remove_tag")
                  : page.pendingAction === "renameType" ? Lang.t("confirm.rename_type")
                  : Lang.t("confirm.remove_type")
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.px(12)
            font.family: Theme.fontFamily
            color: Theme.text2
        }

        footer: Item {
            implicitHeight: 44

            // 取消：清掉待执行动作
            Btn {
                text: Lang.t("confirm.cancel")
                kind: "ghost"
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    page.pendingAction = ""
                    page.pendingId = -1
                    page.pendingOldName = ""
                    page.pendingName = ""
                    confirmDialog.close()
                }
            }

            // 确认：先取出并清空待执行动作 再真正执行
            Btn {
                text: Lang.t("common.ok")
                // 删除类操作用红按钮 重命名不是危险操作 -> 用主色按钮
                kind: page.pendingAction === "renameType" ? "primary" : "danger"
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    var action = page.pendingAction
                    var id = page.pendingId
                    var oldName = page.pendingOldName
                    var name = page.pendingName
                    page.pendingAction = ""
                    page.pendingId = -1
                    page.pendingOldName = ""
                    page.pendingName = ""
                    confirmDialog.close()
                    if (action === "removeType" && id > 0)
                        ConfigBridge.removeType(id)
                    else if (action === "removeTag" && id > 0)
                        ConfigBridge.removeTag(id)
                    else if (action === "renameType" && oldName.length > 0) {
                        // 源类型按名字找（类型名称输入框里那个）-> 改成重命名输入框里的名字
                        // 成功才动输入框：名字输入框跟着走到改名后的类型上 重命名输入框清空
                        if (ConfigBridge.renameType(oldName, name)) {
                            typeNameInput.text = name
                            typeRenameInput.text = ""
                        }
                    }
                }
            }
        }
    }

    // RGB 选色盘弹窗
    Popup {
        id: colorPopup
        width: 300
        height: 320
        x: (page.width - width) / 2
        y: (page.height - height) / 2
        modal: true
        focus: true
        dim: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // 遮罩
        Overlay.modal: Rectangle { color: "#520f1218" }

        background: Rectangle {
            radius: 12
            color: Theme.surface
            border.width: 1
            border.color: Theme.line2
        }

        Column {
            id: cpContent
            width: parent.width
            height: 320

            Item {
                id: cpHead
                width: parent.width
                height: 48

                Text {
                    x: 13
                    anchors.verticalCenter: parent.verticalCenter
                    text: Lang.t("editor.color_title")
                    font.pixelSize: Theme.px(12)
                    font.weight: Font.Bold
                    font.family: Theme.fontFamily
                    color: Theme.text
                }

                Rectangle {
                    width: 26
                    height: 26
                    anchors.right: parent.right
                    anchors.rightMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 6
                    color: cpCloseMouse.containsMouse ? Theme.hover : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        font.pixelSize: Theme.px(16)
                        font.family: Theme.fontFamily
                        color: cpCloseMouse.containsMouse ? Theme.text : Theme.text3
                    }

                    MouseArea {
                        id: cpCloseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: colorPopup.close()   // 关闭且不修改颜色
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.line
                }
            }

            Item {
                id: cpBody
                width: parent.width
                height: cpBodyCol.height + 26

                Column {
                    id: cpBodyCol
                    x: 13
                    y: 13
                    width: parent.width - 26
                    spacing: 11

                    Rectangle {
                        width: parent.width
                        height: 46
                        radius: 9
                        color: page.rgbToHex(page.pickR, page.pickG, page.pickB)
                        border.width: 1
                        border.color: Theme.line2
                    }

                    Item {
                        id: rRow
                        width: parent.width
                        height: 26

                        Text {
                            x: 0
                            width: 14
                            height: parent.height
                            text: "R"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.weight: Font.Bold
                            font.family: Theme.fontFamily
                            color: Theme.text2
                        }

                        Slider {
                            id: rSlider
                            x: 22
                            width: rRow.width - 62
                            height: rRow.height
                            from: 0
                            to: 255
                            stepSize: 1
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0

                            onValueChanged: {
                                page.pickR = Math.round(value)
                                page.syncHexField()
                            }

                            background: Rectangle {
                                x: rSlider.leftPadding
                                y: rSlider.topPadding + rSlider.availableHeight / 2 - height / 2
                                width: rSlider.availableWidth
                                height: 6
                                radius: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "#000000" }
                                    GradientStop { position: 1.0; color: "#ff0000" }
                                }
                            }

                            handle: Rectangle {
                                x: rSlider.leftPadding + rSlider.visualPosition * (rSlider.availableWidth - width)
                                y: rSlider.topPadding + rSlider.availableHeight / 2 - height / 2
                                width: 15
                                height: 15
                                radius: 7.5
                                color: "#ffffff"
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }

                        Text {
                            x: rRow.width - 32
                            width: 32
                            height: parent.height
                            text: page.pickR
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontMono
                            color: Theme.text2
                        }
                    }

                    Item {
                        id: gRow
                        width: parent.width
                        height: 26

                        Text {
                            x: 0
                            width: 14
                            height: parent.height
                            text: "G"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.weight: Font.Bold
                            font.family: Theme.fontFamily
                            color: Theme.text2
                        }

                        Slider {
                            id: gSlider
                            x: 22
                            width: gRow.width - 62
                            height: gRow.height
                            from: 0
                            to: 255
                            stepSize: 1
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0

                            onValueChanged: {
                                page.pickG = Math.round(value)
                                page.syncHexField()
                            }

                            background: Rectangle {
                                x: gSlider.leftPadding
                                y: gSlider.topPadding + gSlider.availableHeight / 2 - height / 2
                                width: gSlider.availableWidth
                                height: 6
                                radius: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "#000000" }
                                    GradientStop { position: 1.0; color: "#00ff00" }
                                }
                            }

                            handle: Rectangle {
                                x: gSlider.leftPadding + gSlider.visualPosition * (gSlider.availableWidth - width)
                                y: gSlider.topPadding + gSlider.availableHeight / 2 - height / 2
                                width: 15
                                height: 15
                                radius: 7.5
                                color: "#ffffff"
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }

                        Text {
                            x: gRow.width - 32
                            width: 32
                            height: parent.height
                            text: page.pickG
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontMono
                            color: Theme.text2
                        }
                    }

                    Item {
                        id: bRow
                        width: parent.width
                        height: 26

                        Text {
                            x: 0
                            width: 14
                            height: parent.height
                            text: "B"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.weight: Font.Bold
                            font.family: Theme.fontFamily
                            color: Theme.text2
                        }

                        Slider {
                            id: bSlider
                            x: 22
                            width: bRow.width - 62
                            height: bRow.height
                            from: 0
                            to: 255
                            stepSize: 1
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0

                            onValueChanged: {
                                page.pickB = Math.round(value)
                                page.syncHexField()
                            }

                            background: Rectangle {
                                x: bSlider.leftPadding
                                y: bSlider.topPadding + bSlider.availableHeight / 2 - height / 2
                                width: bSlider.availableWidth
                                height: 6
                                radius: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "#000000" }
                                    GradientStop { position: 1.0; color: "#0000ff" }
                                }
                            }

                            handle: Rectangle {
                                x: bSlider.leftPadding + bSlider.visualPosition * (bSlider.availableWidth - width)
                                y: bSlider.topPadding + bSlider.availableHeight / 2 - height / 2
                                width: 15
                                height: 15
                                radius: 7.5
                                color: "#ffffff"
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }

                        Text {
                            x: bRow.width - 32
                            width: 32
                            height: parent.height
                            text: page.pickB
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.family: Theme.fontMono
                            color: Theme.text2
                        }
                    }

                    Item {
                        id: hexRow
                        width: parent.width
                        height: 28

                        Text {
                            x: 0
                            width: 32
                            height: parent.height
                            text: "HEX"
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.px(11)
                            font.weight: Font.Bold
                            font.family: Theme.fontFamily
                            color: Theme.text2
                        }

                        TextField {
                            id: hexInput
                            x: 40
                            width: hexRow.width - 40
                            height: 28
                            leftPadding: 9
                            rightPadding: 9
                            maximumLength: 7
                            placeholderText: "#RRGGBB"
                            color: Theme.text
                            font.pixelSize: Theme.px(12)
                            font.family: Theme.fontMono
                            font.capitalization: Font.AllUppercase
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true

                            background: Rectangle {
                                radius: 6
                                color: Theme.surface
                                border.width: 1
                                border.color: hexInput.activeFocus ? Theme.accent : Theme.line2
                            }

                            onTextEdited: page.applyHexText()          // 合法即同步三个滑块
                            onEditingFinished: page.syncHexField()     // 失焦 / 回车回填规范值
                        }
                    }
                }
            }

            Rectangle {
                id: cpActions
                width: parent.width
                height: 50
                color: Theme.surface2

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: Theme.line
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Btn {
                        text: Lang.t("btn.cancel")
                        kind: "secondary"
                        onClicked: colorPopup.close()   // 关闭且不修改颜色
                    }

                    Btn {
                        text: Lang.t("common.ok")
                        kind: "primary"
                        onClicked: page.confirmColorPicker()
                    }
                }
            }
        }
    }
}
