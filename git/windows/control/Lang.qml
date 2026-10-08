pragma Singleton

// 语言单例（界面文案唯一来源）
//
// 取值顺序（配合 core 的 LanguageManager）：
//   1. core 的字典优先：language/<当前语言>.json 里的 id -> str。翻译与切换都由 core 的
//      LanguageManager 负责 —— loadLanguageList("./language") 扫目录列表、loadLanguage 切语言，
//      ConfigBridge 持有它并把 text(id) 暴露给 QML；
//   2. core 字典里没有这个 id 时，落回本文件自带的兜底表（zh_CN / en_US 两份，按当前语言挑）。
//
// 为什么兜底表不能删：language/*.json 目前是旧工程那 73 个键，新 UI 用了 188 个键，
// 两边只重叠 25 个 —— 剩下 163 个键全靠这张表。
//
// 其它约定：
//   - t() 里先读 fallbackTable：它依赖 lang.current，切语言时所有 Lang.t(...) 绑定才会自动重算；
//   - 两边都取不到时回退返回 key 本身（便于一眼看出漏翻译的键）；
//   - %1 / %2 ... 与 %1$d / %2$s 两种占位都支持（源工程 JSON 里两种写法都有）。
//
// 用法：text: Lang.t("btn.addDir")   /   Lang.t("settings.status_line", n, m, mode)

import QtQuick

QtObject {
    id: lang

    // =========================================================================
    // core（LanguageManager）给的语言状态 —— 真值在 core，本文件不存一份
    // =========================================================================

    // 当前语言：core 加载进来的语言名（config.json 的 DefaultLanguage；
    // ConfigBridge.setLanguage -> LanguageManager::loadLanguage 切换后这里跟着变）
    readonly property string current: ConfigBridge.language

    // 可选语言列表：ConfigBridge 扫 ./language 目录得到的文件名主干（已排序）
    readonly property var languages: ConfigBridge.languageList

    // 兜底表挑哪一份：把 core 的语言名（zh-cn / en-us）归一成兜底表的键（zh_CN / en_US）。
    // 认不出的语言一律落回中文兜底
    readonly property var fallbackTable: {
        var code = String(lang.current).replace(/-/g, "_").toLowerCase()
        if (code === "en_us" || code === "en")
            return lang.dictionary["en_US"]
        return lang.dictionary["zh_CN"]
    }

    // ---------- 取文案 ----------
    // core 字典优先，拿不到才落回本文件的兜底表
    // a / b / c / d 依次替换 %1 %2 %3 %4（含 %1$d / %1$s 写法）
    function t(key, a, b, c, d) {
        // 先读兜底表：它依赖 lang.current，切语言时所有 Lang.t(...) 的绑定才会自动重算
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

    // ---------- 字典 ----------
    readonly property var dictionary: ({
        "zh_CN": {
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
            "filter.cleared": "标签筛选已清空（目录恢复全选）",
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
            "confirm.remove_type": "确定删除这个类型？该类型下的标签会一起被移除。",
            "confirm.remove_tag": "确定删除这个标签？已经赋值给文件的记录也会被移除。",
            "confirm.convert_mode": "确定转换存储模式？会对全部受管文件批量改写（Sidecar ⇄ Filename）。",
            "confirm.rename_type": "确定重命名这个类型？类型下的标签会跟着一起改名。",
            "editor.type_color_updated_prefix": "已更新类型颜色：",
            "about.open_github": "打开 GitHub 仓库",
        },
        "en_US": {
            // ===== App / top bar =====
            "app.name": "TagMeow",
            "app.tagline": "File intelligence workspace",
            "stat.files_prefix": "Files ",
            "stat.tags_prefix": "Tags ",

            // ===== Navigation =====
            "browse.files": "Files",
            "nav.directory": "Folders",
            "nav.sync": "Sync",
            "nav.settings": "Settings",
            "tag.library_title": "Tag library",

            // ===== Common =====
            "common.count_suffix": " items",
            "common.rename": "Rename",
            "common.know": "Got it",
            "common.reserved": "Reserved",
            "common.ok": "OK",
            "common.done": "Done",
            "common.refresh": "Refresh",
            "btn.ok": "OK",
            "btn.cancel": "Cancel",

            // ===== Browse =====
            "browse.eyebrow": "Workspace / Files",
            "browse.desc": "Find local files by tag. Drag a tag onto a file to assign it.",
            "browse.current_filter": "Current filters",
            "cnt.include": "Include",
            "cnt.exclude": "Exclude",
            "cnt.only": "Only",
            "cnt.tip": "Drag tags here",
            "browse.search": "Search",
            "browse.clear": "Clear",
            "browse.view_list": "Switched to list view",
            "browse.view_grid_hint": "Grid view only changes styling; the list layout stays the same",
            "browse.readonly": "Read-only",
            "browse.not_editable": "Not editable",
            "browse.rail_note": "Tags come from the tag library. Drag a tag onto any file on the left to assign it; tag names and colors cannot be changed here.",
            "browse.empty_files": "No files in the library yet",
            "tag.library_empty_hint": "Tag library is empty — create one in Tags",
            "browse.footer_drag": "Drag from the tag library on the right onto a file to assign",
            "browse.footer_search": "Quick search",
            "browse.kbd_drag": "Drag",

            // ===== Filter / file toasts =====
            "filter.summary_none": "No filters",
            "filter.added_prefix": "Added to filters: ",
            "filter.already_added": "This tag is already in the same group",
            "filter.removed": "Filter removed",
            "filter.cleared": "Tag filters cleared (folders back to all selected)",
            "dir.selected_count": "%1 selected",
            "dir.none_selected": "None selected",
            "file.removed_prefix": "Removed file row: ",
            "file.tag_exists": "This file already has the tag \"%1\"",
            "file.tag_removed": "Removed tag from %1: %2",
            "file.tag_assigned": "Tag assigned",
            "file.tag_assigned_note": "The tag is written straight into the file record. Tag names, types and colors stay read-only.",

            // ===== Folders =====
            "dir.eyebrow": "Workspace / Folders",
            "dir.desc": "Manage the local folders that take part in indexing.",
            "btn.addDir": "Add Directory",
            "dir.panel_title": "Indexed folders",
            "dir.contains_files": "Contains %1 file(s)",
            "dir.open": "Open",
            "dir.mark_invalid": "Mark invalid",
            "dir.mark_valid": "Mark valid",
            "dir.remove": "Remove",
            "dir.empty": "No indexed folder yet",
            "dir.browser_title": "Pick a folder",
            "dir.add_requires_path": "No folder path received",
            "dir.added": "Folder added and indexed",
            "dir.removed_prefix": "Removed ",

            // ===== Tag library =====
            "tag.eyebrow": "Workspace / Tag library",
            "tag.desc": "Manage tag types and tags: add, delete, rename, with a live preview of every group.",
            "tag.types_title": "Type management",
            "tag.tags_title": "Tag management",
            "tag.col_name": "Name",
            "tag.col_tag_count": "Tags",
            "tag.col_actions": "Actions",
            "tag.rename_placeholder": "New name",
            "tag.pick_type_first": "Pick a type first",
            "tag.pick_tag_first": "Pick a tag first",
            "tip.tag": "Tag:",
            "editor.color_label": "Color",
            "editor.type_name_label": "Type name",
            "editor.tag_name_label": "Tag name",
            "editor.group_label": "Group",
            "editor.add": "Add",
            "editor.delete": "Delete",
            "editor.color_title": "Pick a color",
            "editor.empty_types": "No type yet — fill the name and color, then click Add",
            "editor.empty_tags": "No tag under this type yet",
            "editor.pick_group_first": "Pick a type first",
            "editor.name_required": "Type name cannot be empty",
            "editor.tag_name_required": "Tag name cannot be empty",
            "editor.type_added_prefix": "Type added: ",
            "editor.type_deleted_prefix": "Type deleted: ",
            "editor.type_renamed_prefix": "Renamed to ",
            "editor.tag_added_prefix": "Tag added: ",
            "editor.tag_deleted_prefix": "Tag deleted: ",
            "editor.color_updated_prefix": "Color updated: ",

            // ===== Sync =====
            "sync.eyebrow": "System / Sync",
            "sync.desc": "Manage server sharing and client downloads over the local network.",
            "sync.server_panel": "Server",
            "sync.client_panel": "Client",
            "sync.server_name_label": "Server name",
            "sync.share_dir_label": "Shared folder",
            "sync.send_queue": "Send queue",
            "sync.disconnect_client": "Disconnect the client",
            "sync.found_servers": "Discovered servers",
            "sync.serverName": "Enter server name",
            "sync.dirPath": "Enter directory path",
            "sync.start": "Start",
            "sync.stop": "Stop",
            "sync.choose_dir": "Choose folder",
            "sync.save_path_placeholder": "Folder to save downloaded files",
            "sync.save_to": "Save to",
            "sync.change_path": "Change save folder",
            "sync.path_as_root": "Add as managed folder",
            "sync.queue_empty": "Nothing queued yet",
            "sync.device_empty": "No device scanned yet",
            "sync.scan": "Scan local network devices",
            "sync.downloadSel": "Download selected device",
            "sync.clearRecords": "Clear download record cache",
            "sync.disconnect": "Disconnect device",
            "sync.queue_removed": "Removed from the send queue",
            "sync.server_removed": "Server removed",
            "sync.clear_cache": "Clear cache",
            "sync.download_action": "Download",
            "sync.no_server": "No server found",
            "sync.enqueue": "Add to send queue",
            "sync.enqueued": "Queued: %1",
            "sync.dir_invalid": "Not a valid folder: %1",
            "sync.server_waiting": "Server: queue empty, waiting for folders...",
            "sync.server_session_end": "Server: session ended",
            "sync.server_session_error": "Server: session error or interrupted",
            "sync.start_failed": "Server start failed: %1",
            "sync.scanning": "Scanning the LAN...",
            "sync.scan_done": "Scan finished: %1 device(s)",
            "sync.no_selection": "Scan and select a device first",
            "sync.downloading": "Downloading from %1 ...",
            "sync.download_empty": "Server queue is empty - nothing to download",
            "sync.download_failed": "Download failed or disconnected: %1",
            "sync.client_disconnected": "Client disconnected",
            "sync.records_cleared": "Download records cleared",
            "sync.task_done": "Task finished: %1",
            "sync.change_path_busy": "Cannot change the save folder while downloading",
            "sync.path_changed": "Save folder changed to: %1",
            "sync.download_dir_added": "Added to managed folders: %1",
            "sync.download_dir_unknown": "Nothing downloaded yet - no download folder to add",
            "status.server.disconnected": "Server: client disconnected",
            "status.server.started": "Server started",
            "status.server.stop": "Server stopped",

            // ===== Settings =====
            "settings.eyebrow": "System / Settings",
            "settings.desc": "Adjust app behavior, appearance and data management.",
            "settings.group_general": "General",
            "settings.group_support": "Support and about",
            "settings.group_data": "Data and tools",
            "settings.language": "Language",
            "settings.language_hint": "(tap to switch to the next)",
            "settings.theme": "Theme color",
            "theme.white_day": "Day",
            "theme.black_night": "Night",
            "settings.version": "Version",
            "settings.version_hint": "Read-only - the Version field in config.json",
            "settings.font_size": "Font size",
            "settings.font_size_hint": "Scales the whole UI from the %1 px baseline, saved immediately",
            "theme.light": "Light",
            "theme.dark": "Dark",
            "bar.help": "Help",
            "settings.help_desc": "How the tag model works",
            "settings.about": "About TagMeow",
            "settings.about_desc": "github.com/kakameow/TagMeow",
            "settings.advanced": "Advanced",
            "settings.advanced_desc": "Reserved for developers",
            "settings.status": "Current status",
            "settings.status_line": "%1$d folders · %2$d indexed · %3$s by default",
            "settings.mode": "Mode conversion",
            "settings.mode_current_prefix": "Current ",
            "settings.mode_hint": ", click to convert all managed files",
            "btn.export": "Export Tag Library",
            "settings.export_desc": "Write it out as JSON",
            "btn.import": "Import Tag Library",
            "settings.import_desc": "Restore it from a JSON file",
            "settings.refresh_index": "Rescan index",
            "settings.refresh_desc": "Scan every managed folder again",
            "settings.cleanup": "Clean stale / duplicate rows",
            "settings.cleanup_desc": "Database only — real files are untouched",
            "settings.language_saved_prefix": "Language saved: ",
            "settings.theme_changed_prefix": "Accent changed: ",
            "export.done": "Tag library exported",
            "settings.open_help": "Open help",
            "help.title": "Help",
            "help.body": "1. Add managed folders on the Directories page - the app scans and indexes them.\n2. Create a type first, then tags under it; tags can have their own colors.\n3. Drag a tag onto a file to assign it; filters support include / exclude / only.\n4. The Sync page can start a server or connect to another TagMeow on the LAN.\n5. Settings changes (language / theme / font size / storage mode) are written to config.json immediately.",
            "settings.open_about": "Open the about page",
            "settings.refreshing": "Refreshing the index…",

            // ===== Level browsing (file page loads one folder level at a time) =====
            "browse.up": "Up one level",
            "browse.exit_browse": "Exit browsing",
            "browse.level_hint": "Double-click: folder = go deeper / file = open with the system app. Right-click: more actions",
            "browse.empty_level": "Nothing to show in this folder",
            "browse.enter_dir": "Open folder",
            "browse.open_system": "Open with system app",
            "browse.reveal_system": "Show in folder",
            "browse.not_indexed": "Not indexed",
            "browse.dir_kind": "Folder",
            "tag.search_placeholder": "Search tags by name",
            "tag.search_empty": "No matching tags",
            "sync.download_done": "Download finished",
            "sync.server_off_hint": "The server is not running - start it before adding directories",

            // ===== Destructive action confirmations =====
            "confirm.title": "Confirm",
            "confirm.cancel": "Cancel",
            "confirm.remove_dir": "Remove this managed folder? It is only removed from the list; files on disk stay untouched.",
            "confirm.remove_type": "Delete this type? Every tag under it is removed too.",
            "confirm.remove_tag": "Delete this tag? It is also removed from every file it was assigned to.",
            "confirm.convert_mode": "Convert the storage mode? Every managed file will be rewritten (Sidecar <-> Filename).",
            "confirm.rename_type": "Rename this type? Every tag under it is renamed too.",
            "editor.type_color_updated_prefix": "Type color updated: ",
            "about.open_github": "Open the GitHub repository",
        }
    })
}

// ---------- 源工程里没有、本原型新起的键（两边 JSON 都没有对应文案）----------
// app.tagline
// browse.eyebrow / browse.desc / browse.current_filter / browse.clear / browse.view_list /
// browse.view_grid_hint / browse.readonly / browse.not_editable / browse.rail_note /
// browse.empty_files / browse.footer_drag / browse.footer_search / browse.kbd_drag
// filter.summary_none / filter.added_prefix / filter.already_added / filter.removed / filter.cleared
// dir.selected_count / dir.none_selected / dir.eyebrow / dir.desc / dir.panel_title /
// dir.contains_files / dir.mark_invalid / dir.mark_valid / dir.empty / dir.add_requires_path
// file.removed_prefix / file.tag_exists / file.tag_removed / file.tag_assigned / file.tag_assigned_note
// tag.eyebrow / tag.desc / tag.types_title / tag.tags_title / tag.col_name / tag.col_tag_count /
// tag.col_actions / tag.rename_placeholder / tag.pick_type_first / tag.pick_tag_first
// sync.eyebrow / sync.desc / sync.server_panel / sync.client_panel / sync.server_name_label /
// sync.share_dir_label / sync.send_queue / sync.disconnect_client / sync.found_servers /
// sync.choose_dir / sync.save_path_placeholder / sync.queue_removed / sync.server_removed /
// sync.clear_cache / sync.download_action / sync.no_server / status.server.started
// settings.eyebrow / settings.desc / settings.open_help / settings.open_about /
// settings.refreshing / common.done
