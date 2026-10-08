// 标签库页（参考稿 #page-tags：HTML 1302-1372 行 + CSS .tag-manage-layout / .manage-grid / .manage-card
//   / .input-row / .table-scroll / .manage-table / .display-group-card / .display-tag-pill）
// 表格为自绘（QML 无 <table>）：表头随内容一起滚动，未做 sticky 吸顶
// 只做简单增删（不接后端、不持久化），数据全部来自 Store 单例

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {
    id: page

    // 参考稿 .manage-grid 在窄屏变一列（与 Theme.railWidth 一致用 980 断点）
    readonly property bool oneColumn: width < 980

    // ---------- 固定尺寸比例（窗口锁定 1280×820，内容区可用高度 740：页面被填满，不整页滚动）----------
    readonly property int contentH: 740     // 页面根节点高度 = 内容区可用高度
    readonly property int cardH: 292        // 两张 manage-card 固定高度（17 标题 + 9 + 44 输入行 + 9 + 189 表格 + 24 padding）
    // tag-display 固定可视高度：两列时 740 − 页头 − 卡片 292 − 两处 10 间距 = 376（页头按实测高度，底部不留空）
    // 单列（<980）时两张卡片纵向堆叠，预览区取剩余高度，保证页面总高仍是 740（不溢出、不整页滚动）
    readonly property int displayH: oneColumn
                                    ? Math.max(0, contentH - headerBox.height - (cardH * 2 + 10) - 20)
                                    : contentH - headerBox.height - 10 - cardH - 10

    // ---------- RGB 选色盘（参考稿 #cpBackdrop / .cp-modal，HTML 1603-1639）----------
    // 选色盘当前 RGB（对应参考稿 cpR / cpG / cpB）：打开弹窗时用 Store.typeColor 初始化，不预置颜色
    property int pickR: 0
    property int pickG: 0
    property int pickB: 0

    // 危险操作确认：页级只放一个弹窗，待执行的动作与目标 id 存在 pendingAction / pendingId 里
    // （pendingId 存的是待删的 typeId / tagId 或待改名的 typeId，不是表格行号，删行不影响它）
    // pendingName 只在「重命名类型」时用：确认前先把新名称记下来，输入框本身保持不动
    property string pendingAction: ""
    property int pendingId: -1
    property string pendingName: ""

    // "#abc" / "#AABBCC" → { r, g, b }；非法值回退到 Store.typeColor（仍非法才用黑色兜底）
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

    // 0-255 取整（参考稿 clamp255）
    function clamp255(n) {
        var v = Math.round(Number(n))
        if (isNaN(v))
            v = 0
        return Math.max(0, Math.min(255, v))
    }

    // 单个通道 → 两位大写十六进制
    function toHex2(n) {
        var s = page.clamp255(n).toString(16).toUpperCase()
        return s.length < 2 ? "0" + s : s
    }

    // r / g / b → "#RRGGBB"（大写，参考稿 rgbToHex）
    function rgbToHex(r, g, b) {
        return "#" + page.toHex2(r) + page.toHex2(g) + page.toHex2(b)
    }

    // 打开选色盘：用当前 Store.typeColor 初始化三个滑块（参考稿 openColorPicker）
    function openColorPicker() {
        var rgb = page.hexToRgb(Store.typeColor)
        page.setPickerRgb(rgb.r, rgb.g, rgb.b)
        colorPopup.open()
    }

    // 把 RGB 同步到三个滑块 + HEX 输入框（参考稿 refreshPicker）
    function setPickerRgb(r, g, b) {
        page.pickR = page.clamp255(r)
        page.pickG = page.clamp255(g)
        page.pickB = page.clamp255(b)
        rSlider.value = page.pickR
        gSlider.value = page.pickG
        bSlider.value = page.pickB
        hexInput.text = page.rgbToHex(page.pickR, page.pickG, page.pickB)
    }

    // 回填 HEX 输入框的规范值；输入框正在编辑时不打断（参考稿 refreshPicker 的 activeElement 判断）
    function syncHexField() {
        if (!hexInput.activeFocus)
            hexInput.text = page.rgbToHex(page.pickR, page.pickG, page.pickB)
    }

    // HEX 手输：合法 6 位十六进制立刻同步滑块与预览；非法输入忽略，等失焦回填（参考稿 cpHex 的 input 事件）
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

    // 确定：规范化为大写 "#RRGGBB" 写入 Store.typeColor；
    // 已经选中类型时直接把这个颜色应用到该类型（对应源工程 onResetTypeColorClicked -> setTypeColor）
    function confirmColorPicker() {
        var hex = page.rgbToHex(page.pickR, page.pickG, page.pickB)
        Store.typeColor = hex
        if (Store.selectedTypeId > 0) {
            Store.setTypeColor(Store.selectedTypeId, hex)      // 成功时由 Store 提示"已更新类型颜色"
        } else {
            Store.toast(Lang.t("editor.color_updated_prefix") + hex)
        }
        colorPopup.close()
    }

    // 页面根节点固定为内容区可用高度（740），内部各块定高，因此不产生整页滚动、底部无空白
    implicitHeight: page.contentH

    Column {
        id: col
        width: parent.width
        spacing: 10   // .tag-manage-layout{gap:10px}

        // ---------- 页头 ----------
        PageHeader {
            id: headerBox
            width: col.width
            eyebrow: Lang.t("tag.eyebrow")
            title: Lang.t("tag.library_title")
            desc: Lang.t("tag.desc")
        }

        // ---------- .manage-grid：两列（<980 一列）----------
        // 参考稿 .manage-grid 是 align-items:start（两卡各自内容高度）；按需求改成两列等高、底边对齐
        GridLayout {
            id: grid
            width: col.width
            columns: page.oneColumn ? 1 : 2
            columnSpacing: 10
            rowSpacing: 10

            // ======== 类型管理卡片 ========
            Rectangle {
                id: typeCard
                implicitWidth: (grid.width - 10) / 2
                implicitHeight: page.cardH             // 固定高度：292 = 17 标题 + 9 + 44 输入行 + 9 + 189 表格 + 24 padding
                Layout.fillWidth: true
                Layout.fillHeight: !page.oneColumn    // 单列时不拉高，保持内容高度
                radius: 10
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Column {
                    id: typeCol
                    x: 12
                    y: 12
                    width: parent.width - 24
                    spacing: 9   // .manage-card{gap:9px}

                    Text {
                        text: Lang.t("tag.types_title")
                        height: 17
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.px(12)
                        font.weight: Font.Bold
                        font.family: Theme.fontFamily
                        color: Theme.text
                    }

                    // .input-row：灰底 / 圆角 8 / padding 8 / gap 6
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

                            // .color-block：28×28 / 圆角 7 / 2px 白边 + 1px 外描边；hover 外描边变 Theme.accent
                            // 点击打开 RGB 选色盘（参考稿 colorBlock 的 click → openColorPicker）
                            Rectangle {
                                id: typeColorBlock
                                width: 28
                                height: 28
                                radius: 7
                                color: "transparent"
                                border.width: 1
                                border.color: typeColorMouse.containsMouse ? Theme.accent : Theme.line2

                                // 内层：当前类型色 + 2px 白边（参考稿 border:2px solid #fff）
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

                            // .input-field-sm：类型名称
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
                                    Store.addType(typeNameInput.text, Store.typeColor)
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

                            // .input-field-sm：新名称
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
                                    // 只「装弹」不执行：先校验，缺什么就提示什么，都不进确认弹窗
                                    if (Store.selectedTypeId <= 0) {
                                        Store.toast(Lang.t("tag.pick_type_first"))
                                        return
                                    }
                                    if (typeRenameInput.text.length === 0) {
                                        Store.toast(Lang.t("editor.name_required"))
                                        return
                                    }
                                    page.pendingAction = "renameType"
                                    page.pendingId = Store.selectedTypeId
                                    page.pendingName = typeRenameInput.text
                                    confirmDialog.open()
                                }
                            }
                        }
                    }

                    // .table-scroll：固定高度 189 / 圆角 8 / 1px #eef0f2 / 内部竖向滚动
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

                                // 类型行：model 绑 Store.types.length，用 Store.types[index] 取值保证实时刷新
                                Repeater {
                                    model: Store.types.length

                                    delegate: Item {
                                        id: typeRow
                                        required property int index

                                        readonly property var typeItem: Store.types[index]
                                        readonly property bool selected: Store.selectedTypeId === typeRow.typeItem.typeId
                                        readonly property int tagTotal: Store.tagsOfType(typeRow.typeItem.typeId).length
                                        readonly property real nameColW: Math.max(60, typeRow.width - 156)

                                        width: parent.width
                                        height: 27

                                        // 选中行 #eef1ff（Theme.accentSoft）/ hover #fafbfc（Theme.surface2），选中优先（参考稿 tbody tr:hover）
                                        Rectangle {
                                            anchors.fill: parent
                                            color: typeRow.selected ? Theme.accentSoft
                                                                   : (typeRowMouse.containsMouse ? Theme.surface2 : "transparent")
                                        }

                                        Rectangle {
                                            visible: typeRow.index < Store.types.length - 1
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: Theme.lineSoft
                                        }

                                        // 行点击选中（在「删除」按钮下层，按钮点击不会传到这里）
                                        // 顺带回填：颜色进选色入口、类型名进「重命名」输入框（源工程 onLibraryTypeClicked 的回填行为）
                                        MouseArea {
                                            id: typeRowMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                Store.selectedTypeId = typeRow.typeItem.typeId
                                                Store.typeColor = Store.typeColorOf(typeRow.typeItem.typeId)
                                                typeRenameInput.text = typeRow.typeItem.name
                                            }
                                        }

                                        // .color-dot：12×12
                                        Rectangle {
                                            x: 9
                                            width: 12
                                            height: 12
                                            radius: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: Store.typeColorOf(typeRow.typeItem.typeId)
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
                                visible: Store.types.length === 0
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

            // ======== 标签管理卡片 ========
            Rectangle {
                id: tagCard
                implicitWidth: (grid.width - 10) / 2
                implicitHeight: page.cardH             // 与类型管理卡片同高（292）
                Layout.fillWidth: true
                Layout.fillHeight: !page.oneColumn    // 与类型管理卡片等高（底边对齐）
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

                    // .input-row：所属类型 / 标签名称 / 添加 / 删除
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

                            // 类型下拉：选中项与 Store.selectedTypeId 双向同步
                            ComboBox {
                                id: tagTypeCombo
                                width: 120
                                height: 28
                                model: Store.types
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

                                // 选中的类型被删掉时退回第一个类型（参考稿 populateTypeSelect）
                                function syncIndex() {
                                    for (var i = 0; i < Store.types.length; i++) {
                                        if (Store.types[i].typeId === Store.selectedTypeId) {
                                            tagTypeCombo.currentIndex = i
                                            return
                                        }
                                    }
                                    if (Store.types.length > 0) {
                                        tagTypeCombo.currentIndex = 0
                                        Store.selectedTypeId = Store.types[0].typeId
                                    } else {
                                        tagTypeCombo.currentIndex = -1
                                    }
                                }

                                Component.onCompleted: tagTypeCombo.syncIndex()

                                onActivated: function (index) {
                                    if (index >= 0 && index < Store.types.length)
                                        Store.selectedTypeId = Store.types[index].typeId
                                }

                                Connections {
                                    target: Store
                                    function onSelectedTypeIdChanged() { tagTypeCombo.syncIndex() }
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
                                    Store.addTag(Store.selectedTypeId, tagNameInput.text)
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

                                // 标签行：只列当前选中类型的标签（参考稿 renderTagTable 行为）
                                Repeater {
                                    model: Store.tagsOfType(Store.selectedTypeId).length

                                    delegate: Item {
                                        id: tagRow
                                        required property int index

                                        readonly property var tagItem: Store.tagsOfType(Store.selectedTypeId)[index]
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
                                            visible: tagRow.index < Store.tagsOfType(Store.selectedTypeId).length - 1
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
                                            color: Store.typeColorOf(tagRow.tagItem.typeId)
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
                                visible: Store.tagsOfType(Store.selectedTypeId).length === 0
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

        // ---------- 整页标签预览 .tag-display：固定可视高度（740 下为 376）+ 内部竖向滚动 ----------
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
                spacing: 8   // .tag-display{gap:8px}

                Repeater {
                    model: Store.types.length

                    delegate: Rectangle {
                        id: groupCard
                        required property int index

                        readonly property var typeItem: Store.types[index]
                        readonly property var groupTags: Store.tagsOfType(groupCard.typeItem.typeId)
                        // pill 的 model：{ name, color }（颜色来自所属类型）
                        readonly property var pillItems: {
                            var out = []
                            for (var i = 0; i < groupCard.groupTags.length; i++)
                                out.push({ name: groupCard.groupTags[i].name, color: groupCard.typeItem.color })
                            return out
                        }

                        width: parent.width
                        height: groupCol.height + 18   // .display-group-card{padding:9px 10px}
                        radius: 9
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.line

                        Column {
                            id: groupCol
                            x: 10
                            y: 9
                            width: parent.width - 20
                            spacing: 7

                            // .display-group-header：色块 + 类型名 + 右侧数量
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

                            // .display-tags
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

                                        // .display-tag-pill：高 24 / 圆角 6 / 白底 / 1px Theme.line2；hover 上浮 1px
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

            // 空态：core 还没给出类型数据时预览区显示灰字提示
            Text {
                x: Math.max(0, (displayFlick.width - width) / 2)
                y: Math.max(0, (displayFlick.height - height) / 2)
                visible: Store.types.length === 0
                text: Lang.t("tag.library_empty_hint")
                font.pixelSize: Theme.px(11)
                font.family: Theme.fontFamily
                color: Theme.text3
            }
        }
    }

    // ---------- 危险操作确认弹窗（删除类型 / 删除标签 / 重命名类型共用）----------
    // 行内「删除」按钮与编辑器里的「删除」按钮都只写 pendingAction / pendingId 再 open()，
    // Repeater 里不会一行一个 Dialog；「重命名」还额外把新名称写进 pendingName
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
            // 文案跟着 pendingAction 走：删标签 / 重命名 / 其余（删类型）三种都对
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

            // 取消：清掉待执行动作（含重命名记住的新名称），什么都不做
            Btn {
                text: Lang.t("confirm.cancel")
                kind: "ghost"
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    page.pendingAction = ""
                    page.pendingId = -1
                    page.pendingName = ""
                    confirmDialog.close()
                }
            }

            // 确认：先取出并清空待执行动作，再真正执行
            Btn {
                text: Lang.t("common.ok")
                // 删除类操作用红按钮，重命名不是危险操作 -> 用主色按钮
                kind: page.pendingAction === "renameType" ? "primary" : "danger"
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    var action = page.pendingAction
                    var id = page.pendingId
                    var name = page.pendingName
                    page.pendingAction = ""
                    page.pendingId = -1
                    page.pendingName = ""
                    confirmDialog.close()
                    if (action === "removeType" && id > 0)
                        Store.removeType(id)
                    else if (action === "removeTag" && id > 0)
                        Store.removeTag(id)
                    else if (action === "renameType" && id > 0) {
                        Store.renameType(id, name)
                        typeRenameInput.text = ""
                    }
                }
            }
        }
    }

    // ---------- RGB 选色盘弹窗（参考稿 #cpBackdrop / .cp-modal / .cp-body / .cp-actions）----------
    // 点色块打开；点遮罩（CloseOnPressOutside）/ × / 取消 / Esc（CloseOnEscape）→ 关闭且不改色
    Popup {
        id: colorPopup
        width: 300
        // 高度 = 标题栏 48 + 内容区 222（13 + 46 + 3×26 + 28 + 11×4 + 13）+ 按钮栏 50
        height: 320
        // 居中于标签库页（Popup 默认贴左上角，必须显式定位）
        x: (page.width - width) / 2
        y: (page.height - height) / 2
        modal: true
        focus: true
        dim: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // 遮罩：参考稿 .cp-backdrop 的 rgba(15,18,24,.32)
        Overlay.modal: Rectangle { color: "#520f1218" }

        // .cp-modal：宽 300 / 圆角 12 / 1px Theme.line2 / 白底
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

            // .cp-head：11×13 padding + 下边框 1px Theme.line
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

                // .cp-head .icon-btn：26×26 / 圆角 6
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

            // .cp-body：padding 13 / 行间距 11
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

                    // .cp-preview：高 46 / 圆角 9 / 1px Theme.line2，背景实时跟随 RGB
                    Rectangle {
                        width: parent.width
                        height: 46
                        radius: 9
                        color: page.rgbToHex(page.pickR, page.pickG, page.pickB)
                        border.width: 1
                        border.color: Theme.line2
                    }

                    // .cp-slider-row：R —— 轨道高 6 / 圆角 3 / #000→#f00 渐变 + 15×15 白色圆手柄
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

                            // 值由 setPickerRgb() / applyHexText() 赋值，这里只把拖动结果写回 page.pickR
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

                        // .cp-val：宽 32 / 右对齐 / 等宽
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

                    // .cp-slider-row：G —— 轨道渐变 #000→#0f0
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

                    // .cp-slider-row：B —— 轨道渐变 #000→#00f
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

                    // .cp-hex-row：HEX 标签 + 可编辑输入框（等宽 / 大写 / 高 28）
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

            // .cp-actions：右对齐 取消 / 确定（padding 10×13 + 上边框 + #fafbfc 底）
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
