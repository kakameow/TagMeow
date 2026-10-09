pragma Singleton

// 语言单例（界面文案唯一来源）

// 取值顺序（配合 core 的 LanguageManager）：
//   1. core 的字典优先：language/<当前语言>.json 里的 id -> str 翻译与切换都由 core 的
//      LanguageManager 负责 —— loadLanguageList("./language") 扫目录列表、loadLanguage 切语言，
//      ConfigBridge 持有它并把 text(id) 暴露给 QML；
//   2. core 字典里没有这个 id 时 落回下面这张**中文兜底表**。
//

// 其它约定：
//   - t() 里先读 fallbackTable：切语言时所有 Lang.t(...) 绑定会自动重算
//   - 两边都取不到时回退返回 key 本身（便于一眼看出漏翻译的键）
//   - %1 / %2 ... 与 %1$d / %2$s 两种占位都支持（源工程 JSON 里两种写法都有）
// 用法：text: Lang.t("btn.addDir")   /   Lang.t("settings.status_line", n, m, mode)

import QtQuick

QtObject {
    id: lang


    // core（LanguageManager）的语言状态 真值在 core
    // 当前语言：core 加载进来的语言名（config.json 的 DefaultLanguage
    // ConfigBridge.setLanguage -> LanguageManager::loadLanguage 切换后这里跟着变）
    readonly property string current: ConfigBridge.language

    // 可选语言列表：ConfigBridge 扫 ./language 目录得到的文件名主干（已排序）
    readonly property var languages: ConfigBridge.languageList


    // 取文案
    // core 字典优先 拿不到才落回本文件的兜底表
    // a / b / c / d 依次替换 %1 %2 %3 %4（含 %1$d / %1$s 写法）
    function t(key, a, b, c, d) {
        // 先读兜底表：它依赖 lang.current 切语言时所有 Lang.t(...) 的绑定才会自动重算
        var table = lang.fallbackTable
        // core 的字典（language/*.json）：core 里没这个 id 时 ConfigBridge.text 返回空串
        var text = ConfigBridge.text(String(key))
        if (text === undefined || text === null || text === "")
        text = table ? table[key] : undefined
        if (text === undefined || text === null)
        return key
        var args = [a, b, c, d]
        // %1$d / %1$s 这类先归一成 %1
        text = String(text).replace(/%([1-9])\$[sd]/g, "%$1")
        text = text.replace(/%([1-9])/g, function (match, index) {
        var value = args[Number(index) - 1]
        return value === undefined || value === null ? match : String(value)
        })
        return text
    }

    // 字典
    // core 字典里没有的键才落回这里 按需求统一用中文兜底）
    readonly property var fallbackTable: ({
        // ===== 应用 / 顶栏（android app.name / stat.*）=====
        "app.name": "TagMeow",
        "app.tagline": "文件智能工作区",
        "stat.files_prefix": "文件 ",
        "stat.tags_prefix": "标签 ",

        // ===== 导航（android nav.* / browse.files / tag.library_title）=====
        "browse.files": "文件",
        "nav.directory": "目录",
        "nav.sync": "同步",
        "nav.settings": "设置",
        "tag.library_title": "标签库",

        // ===== 通用（windows btn.ok / btn.cancel，android common.*）=====
        "common.count_suffix": " 项",
        "common.rename": "重命名",
        "common.know": "知道了",
        "common.reserved": "保留",
        "common.ok": "确定",
        "common.done": "完成",
        "common.refresh": "刷新",
        "btn.ok": "确认",
        "btn.cancel": "取消",

        // ===== 文件页（android cnt.* / browse.search / tag.library_empty_hint）=====
        "browse.eyebrow": "工作区 / 文件",
        "browse.desc": "用标签快速定位你的本地文件，标签可以直接拖到文件上完成赋值。",
        "browse.current_filter": "当前筛选",
        "cnt.include": "包含",
        "cnt.exclude": "排除",
        "cnt.only": "只有",
        "cnt.tip": "拖拽标签到这里",
        "browse.search": "搜索",
        "browse.searching": "正在搜索…",
        "browse.clear": "清空",
        "browse.view_list": "已切换到列表视图",
        "browse.view_grid_hint": "网格视图仅切换样式，暂不改变列表布局",
        "browse.readonly": "只读",
        "browse.not_editable": "不可编辑",
        "browse.rail_note": "标签来自标签库。拖动标签到左侧任意文件即可赋值；标签名称与颜色在此界面不可修改。",
        "browse.empty_files": "库里还没有文件",
        "tag.library_empty_hint": "标签库还是空的，先去『标签』里建一个",
        "browse.footer_drag": "从右侧标签库拖动到文件即可赋值",
        "browse.footer_search": "快速搜索",
        "browse.kbd_drag": "拖拽",

        // ===== 筛选 / 文件（Store.qml 里的提示）=====
        "filter.summary_none": "无筛选条件",
        "filter.added_prefix": "已加入筛选：",
        "filter.already_added": "这个标签已经在同一分组里了",
        "filter.removed": "已移除筛选条件",
        "filter.cleared": "标签筛选已清空",
        "dir.selected_count": "已选 %1 项",
        "dir.none_selected": "未选择",
        "file.removed_prefix": "已删除文件行：",
        "file.tag_exists": "这个文件已经有「%1」标签",
        "file.tag_removed": "已从 %1 移除标签：%2",
        "file.tag_assigned": "标签已赋值",
        "file.tag_assigned_note": "标签会直接写入文件记录。标签名称、类型和颜色保持只读，不提供修改入口。",

        // ===== 目录页（windows btn.addDir，android dir.*）=====
        "dir.eyebrow": "工作区 / 目录",
        "dir.desc": "管理参与索引的本地目录。",
        "btn.addDir": "添加目录",
        "dir.panel_title": "索引目录",
        "dir.contains_files": "包含 %1 个文件",
        "dir.open": "打开",
        "dir.mark_invalid": "标记失效",
        "dir.mark_valid": "标记有效",
        "dir.remove": "移除",
        "dir.empty": "还没有索引目录",
        "dir.browser_title": "选择目录",
        "dir.add_requires_path": "没有拿到目录路径",
        "dir.added": "目录已添加并建立索引",
        "dir.removed_prefix": "已移除 ",
        "dir.refreshed": "已重新扫描 %1",
        "dir.open_pending": "打开 %1：文件页还没接上，先记着",

        // ===== 标签库页（android editor.* / tag.*）=====
        "tag.eyebrow": "工作区 / 标签库",
        "tag.desc": "管理标签类型与标签，支持添加、删除、重命名，并实时预览标签组。",
        "tag.types_title": "类型管理",
        "tag.tags_title": "标签管理",
        "tag.col_name": "名称",
        "tag.col_tag_count": "标签数",
        "tag.col_actions": "操作",
        "tag.rename_placeholder": "新名称",
        "tag.pick_type_first": "请先选择一个类型",
        "tag.type_added": "类型已添加：%1",
        "tag.type_added_color_fixed": "类型已添加，颜色格式非法，已改用默认色：%1",
        "tag.type_color_reset": "类型已存在，颜色已更新：%1",
        "tag.type_removed": "类型已删除：%1",
        "tag.type_renamed": "类型已重命名：%1",
        "tag.rename_failed": "重命名失败（新名字可能已存在）：%1",
        "tag.color_updated": "类型颜色已更新：%1",
        "tag.color_failed": "颜色更新失败：%1",
        "tag.tag_added": "标签已添加：%1",
        "tag.tag_exists": "标签已存在（标签全局唯一）：%1",
        "tag.tag_removed": "标签已删除：%1",
        "tag.pick_tag_first": "请先选择一个标签",
        "tip.tag": "标签:",
        "editor.color_label": "颜色",
        "editor.type_name_label": "类型名称",
        "editor.tag_name_label": "标签名",
        "editor.group_label": "所属类型",
        "editor.add": "添加",
        "editor.delete": "删除",
        "editor.color_title": "选择颜色",
        "editor.empty_types": "还没有类型，填好名称和颜色后点下面的「添加」",
        "editor.empty_tags": "这个类型下还没有标签",
        "editor.pick_group_first": "先选一个所属类型",
        "editor.name_required": "类型名称不能为空",
        "editor.tag_name_required": "标签名称不能为空",
        "editor.type_added_prefix": "已添加类型：",
        "editor.type_deleted_prefix": "已删除类型 ",
        "editor.type_renamed_prefix": "已重命名为 ",
        "editor.tag_added_prefix": "已添加标签：",
        "editor.tag_deleted_prefix": "已删除标签 ",
        "editor.color_updated_prefix": "已更新颜色：",

        // ===== 同步页（windows sync.* / status.*，android sync.*）=====
        "sync.eyebrow": "系统 / 同步",
        "sync.desc": "管理服务端共享与客户端下载，支持局域网内文件与标签同步。",
        "sync.server_panel": "服务端",
        "sync.client_panel": "客户端",
        "sync.server_name_label": "服务器名称",
        "sync.share_dir_label": "共享目录",
        "sync.send_queue": "发送队列",
        "sync.disconnect_client": "断开客户端连接",
        "sync.found_servers": "扫描到的服务端",
        "sync.serverName": "输入服务器名称",
        "sync.dirPath": "输入目录路径",
        "sync.start": "启动",
        "sync.stop": "关闭",
        "sync.choose_dir": "选择目录",
        "sync.save_path_placeholder": "下载文件的保存路径",
        "sync.save_to": "保存到",
        "sync.change_path": "更改保存目录",
        "sync.path_as_root": "加入管理目录",
        "sync.queue_empty": "还没有要发送的目录",
        "sync.device_empty": "还没有扫描到设备",
        "sync.scan": "搜索局域网设备",
        "sync.downloadSel": "下载选中设备",
        "sync.clearRecords": "清除下载记录缓存",
        "sync.disconnect": "断开连接设备",
        "sync.queue_removed": "已从发送队列移除",
        "sync.server_removed": "已移除服务端",
        "sync.clear_cache": "删除缓存",
        "sync.download_action": "下载",
        "sync.no_server": "未发现服务端",
        "sync.enqueue": "加入发送队列",
        "sync.enqueued": "已入队：%1",
        "sync.dir_invalid": "不是有效目录：%1",
        "sync.server_waiting": "服务端：队列为空，等待目录…",
        "sync.server_session_end": "服务端：会话结束",
        "sync.server_session_error": "服务端：会话出错或中断",
        "sync.start_failed": "服务端启动失败：%1",
        "sync.scanning": "正在扫描局域网…",
        "sync.scan_done": "扫描完成，%1 台设备",
        "sync.no_selection": "请先扫描并选中一台设备",
        "sync.downloading": "正在下载 %1 …",
        "sync.download_empty": "服务端队列为空，本次没有文件",
        "sync.download_failed": "下载失败或断开：%1",
        "sync.client_disconnected": "客户端已断开连接",
        "sync.records_cleared": "已清除下载记录",
        "sync.task_done": "任务完成：%1",
        "sync.change_path_busy": "正在下载，不能更改保存目录",
        "sync.path_changed": "保存目录已改为：%1",
        "sync.download_dir_added": "已加入管理目录：%1",
        "sync.download_dir_unknown": "还没有下载过，找不到具体下载目录",
        "status.server.disconnected": "服务端：已断开连接设备",
        "status.server.started": "服务端已启动",
        "status.server.stop": "服务器已停止",

        // ===== 设置页（android settings.*，windows btn.export / btn.import / theme.*）=====
        "settings.eyebrow": "系统 / 设置",
        "settings.desc": "调整应用行为、外观和数据管理策略。",
        "settings.group_general": "常规",
        "settings.group_support": "支持与关于",
        "settings.group_data": "数据与工具",
        "settings.language": "语言",
        "settings.language_hint": "（点击切换下一种）",
        "settings.theme": "主题配色",
        "theme.white_day": "白昼",
        "theme.black_night": "暗夜",
        "settings.version": "版本",
        "settings.version_hint": "读取 config.json 的 Version 字段，只读",
        "settings.font_size": "字体大小",
        "settings.font_size_hint": "缩放整个界面的字号（基准 %1 px），改动立刻写入 config.json",
        "theme.light": "浅色",
        "theme.dark": "深色",
        "bar.help": "帮助",
        "settings.help_desc": "标签模型与使用说明",
        "settings.about": "关于 TagMeow",
        "settings.about_desc": "github.com/kakameow/TagMeow",
        "settings.advanced": "高级选项",
        "settings.advanced_desc": "开发者功能预留",
        "settings.status": "当前状态",
        "settings.status_line": "目录 %1$d 个 · 索引 %2$d 项 · 默认 %3$s",
        "settings.mode": "模式转换",
        "settings.mode_current_prefix": "当前 ",
        "settings.mode_hint": "，点击批量转换全部受管理文件",
        "btn.export": "导出标签库",
        "settings.export_desc": "导出为 JSON 文件",
        "btn.import": "导入标签库",
        "settings.import_desc": "从 JSON 文件恢复标签库",
        "settings.refresh_index": "刷新索引",
        "settings.refresh_desc": "重新扫描全部受管理目录",
        "settings.cleanup": "清除失效 / 重复数据",
        "settings.cleanup_desc": "只清理数据库记录，不动真实文件",
        "settings.language_saved_prefix": "语言偏好已记录：",
        "settings.theme_changed_prefix": "主题已切换：",
        "export.done": "标签库已导出",
        "settings.open_help": "打开帮助",
        "help.title": "帮助",
        "help.body": "1. 「目录」页添加受管目录，程序会扫描并建立索引。\n2. 「标签」页先建类型，再在类型下建标签，标签可以设颜色。\n3. 「文件」页把标签拖到文件上即可赋值；筛选区支持包含 / 排除 / 仅看三种条件。\n4. 「同步」页可以启动服务端，或连接局域网内的其他 TagMeow。\n5. 设置页的改动（语言 / 主题 / 字号 / 存储模式）会立刻写入 config.json。",
        "settings.open_about": "打开关于页面",
        "settings.refreshing": "正在刷新索引…",

        // ===== 层级浏览（文件页按目录层级动态加载）=====
        "browse.up": "返回上一层",
        "browse.exit_browse": "退出层级浏览",
        "browse.level_hint": "双击：目录进入下一层 / 文件用系统方式打开；右键：更多操作",
        "browse.empty_level": "这个目录里没有可显示的内容",
        "browse.enter_dir": "进入该目录",
        "browse.open_system": "用系统方式打开",
        "browse.reveal_system": "在文件夹中显示",
        "browse.not_indexed": "未索引",
        "browse.dir_kind": "目录",
        "tag.search_placeholder": "按名字搜索标签",
        "tag.search_empty": "没有匹配的标签",
        "sync.download_done": "下载完成",
        "sync.server_off_hint": "服务端没在运行，先启动服务端再添加目录",

        // ===== 危险操作确认弹窗 =====
        "confirm.title": "确认操作",
        "confirm.cancel": "取消",
        "confirm.remove_dir": "确定移除这个受管目录？只从列表里移除，磁盘上的文件不会动。",
        "confirm.refresh_dir": "确定重新扫描这个目录？会按磁盘现状重建该目录的索引记录（不动磁盘文件）。",
        "confirm.remove_type": "确定删除这个类型？该类型下的标签会一起被移除。",
        "confirm.remove_tag": "确定删除这个标签？已经赋值给文件的记录也会被移除。",
        "confirm.convert_mode": "确定转换存储模式？会对全部受管文件批量改写（Sidecar ⇄ Filename）。",
        "confirm.rename_type": "确定重命名这个类型？类型下的标签会跟着一起改名。",
        "editor.type_color_updated_prefix": "已更新类型颜色：",
        "about.open_github": "打开 GitHub 仓库",
    })
}
