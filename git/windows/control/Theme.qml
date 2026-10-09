pragma Singleton

import QtQuick

// TagMeow 新 UI 配色单例：日间 / 夜间两套颜色字典 + 与 Palette.js 同名的对外属性
// 用法：调用方把原来的「Palette 别名 + 点号」引用直接换成 Theme.xxx 设置页改 Theme.dark 即整界面实时换色

QtObject {
    id: theme

    // false = 日间 默认; true = 夜间
    property bool dark: false

    //  全局字号缩放（由 ConfigBridge.fontSize 驱动 基准 12 -> 1.0）
    readonly property int baseFontSize: ConfigBridge.fontSize > 0 ? ConfigBridge.fontSize : 12
    readonly property real fontScale: baseFontSize / 12


    function px(n) {
        return Math.round(n * baseFontSize / 12)
    }

    // 日间表
    readonly property var day: ({
        "bg": "#f4f5f7",
        "surface": "#ffffff",
        "surface2": "#fafbfc",
        "surface3": "#f0f1f4",
        "hover": "#eef0f3",
        "text": "#191b1f",
        "text2": "#5c6169",
        "text3": "#8b9098",
        "line": "#e6e8ec",
        "line2": "#d9dce1",
        "accent": "#4b6bff",
        "accentHover": "#3f5ff0",
        "accentSoft": "#eef1ff",
        "accentText": "#2f4cd6",
        "danger": "#d84e59",
        "dangerLine": "#f4dce0",
        "ok": "#3fa06b",
        "chipBlueBg": "#eef3ff",
        "chipBlueLine": "#dbe3ff",
        "chipBlueText": "#3657d1",
        "chipGreenBg": "#edf8f1",
        "chipGreenLine": "#d9eedf",
        "chipGreenText": "#397955",
        "chipPlainBg": "#f3f4f6",
        "chipPlainLine": "#e8eaed",
        "chipPlainText": "#5d636b",
        "filterGroupBg": { "include": "#edf7f0", "exclude": "#fff1f2", "only": "#eef3ff" },
        "filterGroupLine": { "include": "#d9eee0", "exclude": "#f4dce0", "only": "#dde5ff" },
        "pageBg": "#f5f6f8",
        "surfaceInset": "#f7f8fa",
        "surfaceInsetHover": "#f1f3f6",
        "lineSoft": "#f0f1f3",
        "borderFaint": "#e4e7eb",
        "controlLine": "#cfd4da",
        "scrollThumb": "#d3d7dd",
        "scrollThumbHover": "#c3c8cf",
        "iconFaint": "#b0b5bc",
        "textDeep": "#3d434a",
        "textPath": "#555b63",
        "textMid": "#646a72",
        "textDim": "#7b8188",
        "textGhost": "#9ca2a9",
        "tintBlue": "#edf4ff",
        "tintPurple": "#f5efff",
        "tintRed": "#fff0f0",
        "tintGreen": "#e5f7ee",
        "tintGray": "#eceef0",
        "dragLine": "#9cafef",
        "dragStroke": "#7892e8",
        "dangerSoft": "#fff0f1",
        "accentPress": "#3552d8",
        "accentLineSoft": "#c3ceff",
        "accentIcon": "#7b8ce8",
        "okDot": "#46b77a",
        "black": "#000000",
        "white": "#ffffff",
        "page_bg": "#f5f6f8",
        "segmented_bg": "#eceff3",
        "inset_bg": "#f0f2f5",
        "divider": "#edf0f3",
        "border": "#e2e6ee",
        "border_soft": "#e0e3e7",
        "text_primary": "#20242b",
        "text_secondary": "#8c939f",
        "text_medium": "#59616e",
        "text_section": "#656d79",
        "text_faint": "#9aa1ab",
        "text_button": "#5a626d",
        "text_button_2": "#626a75",
        "text_hint_strong": "#b6bcc6",
        "on_primary_text": "#ffffff",
        "primary_button_bg": "#20242b",
        "danger_text": "#df6876",
        "filter_include_bg": "#eef8ff",
        "filter_include_border": "#8ec8f7",
        "filter_exclude_bg": "#fff2f2",
        "filter_exclude_border": "#f2a2a2",
        "filter_only_bg": "#f3f0ff",
        "filter_only_border": "#b9a8ff"
    })

    // 夜间表
    readonly property var night: ({
        "bg": "#202124",
        "surface": "#2a2c30",
        "surface2": "#24262a",
        "surface3": "#2f3237",
        "hover": "#34373c",
        "text": "#e8eaed",
        "text2": "#b0b6be",
        "text3": "#838a93",
        "line": "#3a3e44",
        "line2": "#464a51",
        "accent": "#5c74f0",
        "accentHover": "#6e85ff",
        "accentSoft": "#262d45",
        "accentText": "#a9b8ff",
        "danger": "#f07a83",
        "dangerLine": "#4a2b30",
        "ok": "#4fbf8b",
        "chipBlueBg": "#222b40",
        "chipBlueLine": "#33405e",
        "chipBlueText": "#a3b7ff",
        "chipGreenBg": "#1f3228",
        "chipGreenLine": "#2d4838",
        "chipGreenText": "#82cfa4",
        "chipPlainBg": "#2c2f34",
        "chipPlainLine": "#3a3e44",
        "chipPlainText": "#aeb4bc",
        "filterGroupBg": { "include": "#1f3129", "exclude": "#3a2427", "only": "#262b45" },
        "filterGroupLine": { "include": "#2f4b3c", "exclude": "#5c3339", "only": "#3b4675" },
        "pageBg": "#202124",
        "surfaceInset": "#232528",
        "surfaceInsetHover": "#2f3237",
        "lineSoft": "#363a40",
        "borderFaint": "#3a3e44",
        "controlLine": "#4a4f57",
        "scrollThumb": "#4a4e55",
        "scrollThumbHover": "#5e636b",
        "iconFaint": "#727880",
        "textDeep": "#c6cbd2",
        "textPath": "#aeb4bc",
        "textMid": "#a3a9b1",
        "textDim": "#8f959d",
        "textGhost": "#828892",
        "tintBlue": "#1e2a3f",
        "tintPurple": "#2a2140",
        "tintRed": "#3b2427",
        "tintGreen": "#1e3329",
        "tintGray": "#2b2d31",
        "dragLine": "#3e4e86",
        "dragStroke": "#6e86e0",
        "dangerSoft": "#3b2427",
        "accentPress": "#2f4cd6",
        "accentLineSoft": "#3c4a8c",
        "accentIcon": "#93a6ff",
        "okDot": "#4fbf8b",
        "black": "#000000",
        "white": "#ffffff",
        "page_bg": "#202124",
        "segmented_bg": "#24262a",
        "inset_bg": "#2c2f34",
        "divider": "#363a40",
        "border": "#43464c",
        "border_soft": "#3a3e44",
        "text_primary": "#e8eaed",
        "text_secondary": "#9aa1ab",
        "text_medium": "#b4b9c1",
        "text_section": "#a9b0b9",
        "text_faint": "#7c828b",
        "text_button": "#c2c7ce",
        "text_button_2": "#b8bec6",
        "text_hint_strong": "#5a6069",
        "on_primary_text": "#16181c",
        "primary_button_bg": "#e8eaed",
        "danger_text": "#f07a83",
        "filter_include_bg": "#1e2a3a",
        "filter_include_border": "#35507a",
        "filter_exclude_bg": "#3a2326",
        "filter_exclude_border": "#6a3a40",
        "filter_only_bg": "#272a45",
        "filter_only_border": "#464e86"
    })

    // 对外属性
    readonly property color bg: dark ? night.bg : day.bg
    readonly property color surface: dark ? night.surface : day.surface
    readonly property color surface2: dark ? night.surface2 : day.surface2
    readonly property color surface3: dark ? night.surface3 : day.surface3
    readonly property color hover: dark ? night.hover : day.hover
    readonly property color text: dark ? night.text : day.text
    readonly property color text2: dark ? night.text2 : day.text2
    readonly property color text3: dark ? night.text3 : day.text3
    readonly property color line: dark ? night.line : day.line
    readonly property color line2: dark ? night.line2 : day.line2
    readonly property color accent: dark ? night.accent : day.accent
    readonly property color accentHover: dark ? night.accentHover : day.accentHover
    readonly property color accentSoft: dark ? night.accentSoft : day.accentSoft
    readonly property color accentText: dark ? night.accentText : day.accentText
    readonly property color danger: dark ? night.danger : day.danger
    readonly property color dangerLine: dark ? night.dangerLine : day.dangerLine
    readonly property color ok: dark ? night.ok : day.ok
    readonly property color pageBg: dark ? night.pageBg : day.pageBg

    readonly property color chipBlueBg: dark ? night.chipBlueBg : day.chipBlueBg
    readonly property color chipBlueLine: dark ? night.chipBlueLine : day.chipBlueLine
    readonly property color chipBlueText: dark ? night.chipBlueText : day.chipBlueText
    readonly property color chipGreenBg: dark ? night.chipGreenBg : day.chipGreenBg
    readonly property color chipGreenLine: dark ? night.chipGreenLine : day.chipGreenLine
    readonly property color chipGreenText: dark ? night.chipGreenText : day.chipGreenText
    readonly property color chipPlainBg: dark ? night.chipPlainBg : day.chipPlainBg
    readonly property color chipPlainLine: dark ? night.chipPlainLine : day.chipPlainLine
    readonly property color chipPlainText: dark ? night.chipPlainText : day.chipPlainText

    readonly property var filterGroupBg: dark ? night.filterGroupBg : day.filterGroupBg
    readonly property var filterGroupLine: dark ? night.filterGroupLine : day.filterGroupLine

    // 原界面硬编码 现归入字典的中性色
    readonly property color surfaceInset: dark ? night.surfaceInset : day.surfaceInset
    readonly property color surfaceInsetHover: dark ? night.surfaceInsetHover : day.surfaceInsetHover
    readonly property color lineSoft: dark ? night.lineSoft : day.lineSoft
    readonly property color borderFaint: dark ? night.borderFaint : day.borderFaint
    readonly property color controlLine: dark ? night.controlLine : day.controlLine
    readonly property color scrollThumb: dark ? night.scrollThumb : day.scrollThumb
    readonly property color scrollThumbHover: dark ? night.scrollThumbHover : day.scrollThumbHover
    readonly property color iconFaint: dark ? night.iconFaint : day.iconFaint
    readonly property color textDeep: dark ? night.textDeep : day.textDeep
    readonly property color textPath: dark ? night.textPath : day.textPath
    readonly property color textMid: dark ? night.textMid : day.textMid
    readonly property color textDim: dark ? night.textDim : day.textDim
    readonly property color textGhost: dark ? night.textGhost : day.textGhost
    readonly property color tintBlue: dark ? night.tintBlue : day.tintBlue
    readonly property color tintPurple: dark ? night.tintPurple : day.tintPurple
    readonly property color tintRed: dark ? night.tintRed : day.tintRed
    readonly property color tintGreen: dark ? night.tintGreen : day.tintGreen
    readonly property color tintGray: dark ? night.tintGray : day.tintGray
    readonly property color dragLine: dark ? night.dragLine : day.dragLine
    readonly property color dragStroke: dark ? night.dragStroke : day.dragStroke
    readonly property color dangerSoft: dark ? night.dangerSoft : day.dangerSoft
    readonly property color accentPress: dark ? night.accentPress : day.accentPress
    readonly property color accentLineSoft: dark ? night.accentLineSoft : day.accentLineSoft
    readonly property color accentIcon: dark ? night.accentIcon : day.accentIcon
    readonly property color okDot: dark ? night.okDot : day.okDot

    readonly property color black: dark ? night.black : day.black
    readonly property color white: dark ? night.white : day.white
    readonly property color page_bg: dark ? night.page_bg : day.page_bg
    readonly property color segmented_bg: dark ? night.segmented_bg : day.segmented_bg
    readonly property color inset_bg: dark ? night.inset_bg : day.inset_bg
    readonly property color divider: dark ? night.divider : day.divider
    readonly property color border: dark ? night.border : day.border
    readonly property color border_soft: dark ? night.border_soft : day.border_soft
    readonly property color text_primary: dark ? night.text_primary : day.text_primary
    readonly property color text_secondary: dark ? night.text_secondary : day.text_secondary
    readonly property color text_medium: dark ? night.text_medium : day.text_medium
    readonly property color text_section: dark ? night.text_section : day.text_section
    readonly property color text_faint: dark ? night.text_faint : day.text_faint
    readonly property color text_button: dark ? night.text_button : day.text_button
    readonly property color text_button_2: dark ? night.text_button_2 : day.text_button_2
    readonly property color text_hint_strong: dark ? night.text_hint_strong : day.text_hint_strong
    readonly property color on_primary_text: dark ? night.on_primary_text : day.on_primary_text
    readonly property color primary_button_bg: dark ? night.primary_button_bg : day.primary_button_bg
    readonly property color danger_text: dark ? night.danger_text : day.danger_text
    readonly property color filter_include_bg: dark ? night.filter_include_bg : day.filter_include_bg
    readonly property color filter_include_border: dark ? night.filter_include_border : day.filter_include_border
    readonly property color filter_exclude_bg: dark ? night.filter_exclude_bg : day.filter_exclude_bg
    readonly property color filter_exclude_border: dark ? night.filter_exclude_border : day.filter_exclude_border
    readonly property color filter_only_bg: dark ? night.filter_only_bg : day.filter_only_bg
    readonly property color filter_only_border: dark ? night.filter_only_border : day.filter_only_border

    readonly property int sidebar: 212
    readonly property int sidebarNarrow: 190
    readonly property int rail: 264
    readonly property int railNarrow: 240
    readonly property int topH: 52
    readonly property int pad: 14
    readonly property int padNarrow: 12
    readonly property int padCompact: 10
    readonly property int contentMax: 1240
    readonly property int radiusCard: 10

    readonly property string fontFamily: "Segoe UI Variable Text, Segoe UI, Microsoft YaHei, sans-serif"
    readonly property string fontMono: "Consolas, ui-monospace, monospace"

    // 断点
    function sidebarWidth(w) {
        return w < 1180 ? sidebarNarrow : sidebar
    }

    function railWidth(w) {
        return w < 980 ? 0 : (w < 1180 ? railNarrow : rail)
    }

    function padFor(w) {
        return w < 760 ? padCompact : (w < 1180 ? padNarrow : pad)
    }
}
