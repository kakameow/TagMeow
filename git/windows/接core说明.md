# 新 UI 接 core 库：桥接层说明

产出目录：`C:\Users\h\Desktop\.perf_test\stage\windows\`（把这 5 个文件覆盖进 `git/windows/` 即可；`app.rc`、`img/`、`control/`、`Main.qml` 等原样保留）

## 1. 文件职责

| 文件 | 职责 |
| --- | --- |
| `corebridge.h` / `corebridge.cpp` | `CoreBridge : QObject`。内部持有 `DirectoryConfigManager` + `TagServe`（+ 惰性 `SyncServer`）。对外只暴露 `QVariantList`/`QString`/`bool`：`files/types/tags/dirs/totalFileCount/serverRunning/storeMode/lastError` 属性，数据操作用 `Q_INVOKABLE`。路径 `QString` ↔ core `std::filesystem::path`(UTF-8, 正斜杠) 的双向转换、错误汇总 (`errorOccurred`)、类型/标签的稳定 id 分配都在这里 |
| `Store.qml` | **适配层**（`pragma Singleton`）。对外属性/函数与旧 mock **同名同形**，5 个页面零改动。数据 4 个属性直连 `Core.*`；`currentPage/filters/dirChoices/selectedTypeId/selectedTagId/typeColor` 仍是纯 UI 态；`Core.errorOccurred` 转成本仓的 `toastRequested`，`Core.fileTagAssigned` 补上颜色转成 `tagAssigned` |
| `main.cpp` | `QDir().mkpath("config")` → 建 `CoreBridge` → `qmlRegisterSingletonInstance("Core",1,0,"Core", &bridge)` → 建引擎并 `loadFromModule("windows","Main")`。保留 `objectCreationFailed` 与 `app.setWindowIcon(QIcon(":/img/tagmeow.svg"))` |
| `CMakeLists.txt` | 新 UI 版本 + core 源码（`../core/src/*.cpp`）、`corebridge.*`、`sqlite-amalgamation-3530400/sqlite3.c`、nlohmann / asio 的 include，Windows 下链接 `ws2_32 mswsock iphlpapi`；`LANGUAGES C CXX`（sqlite3.c 是 C）。保留 `Store.qml` 单例声明、`img/tagmeow.svg` 资源、`app.rc` |
| `接core说明.md` | 本文 |

注册方式选了 **`qmlRegisterSingletonInstance`**（任务给的两个选项之一）：C++ 侧一辈子只有一个桥，`Core` 在任何 QML 绑定求值前就已存在，没有"Store 内联 `CoreBridge{}` 时 `core` 可能还是 null"的时序风险；对象声明在 `QQmlApplicationEngine` 之前 → 析构在引擎之后，满足"必须活过引擎"。

## 2. Store API → core 对照

| Store（页面调用） | 内部实现 | core 调用 |
| --- | --- | --- |
| `files` / `types` / `tags` / `dirs` | 直连 `Core.*` 属性 | `TagServe::searchByTags` / `getTypeTag`+`getTypeColor` / `DirectoryConfigManager::getDirectories` |
| `fileCount()` | `files.length`（跟随最近一次查询） | — |
| `totalFileCount()`（新增） | `Core.totalFileCount` | `searchByTags`（哨兵 exclude 全量）+ 前缀统计 |
| `tagCount()` / `tagsOfType()` / `tagColor()` / `typeName()` / `typeColorOf()` | QML 在 `types`/`tags` 上查 | — |
| `filters` 增删 / `clearFilters()` / `toggleDirChoice()` | 改 UI 态后 `runQuery()` | `TagServe::searchByTags`（`include_/exclude_/only_/dirs_`） |
| `addFile(name,dir)` | `Core.addRoot(dir)` | `DirectoryConfigManager::addDirectory` + `TagServe::addRoot`（+`saveToFile`） |
| `removeFile(i)` | `Core.removeFile(path)` | `TagServe::removeFile`（**只删索引记录**） |
| `assignTag(i,tag)` | `Core.addFileTag(path,tag)` | `TagServe::addFileTag`（+`getLastFilePath` 回报真实路径） |
| `removeTagFromFile(i,tag)` | `Core.removeFileTag` | `TagServe::removeFileTag` |
| `addDir(n,p)` / `removeDir(i)` | `Core.addRoot` / `Core.removeRoot` | `TagServe::addRoot`/`removeRoot` + `DirectoryConfigManager::add/removeDirectory` + `saveToFile` |
| `toggleDirValid(i)` | `Core.revalidateDirs()` | `DirectoryConfigManager::loadFromFile`（重算 `is_valid_`，**语义已变**：core 没有"手动标记失效"） |
| `addType(n,c)` | `Core.addType` | `TagServe::addType` + `saveTag` |
| `removeType(id)` / `renameType(id,n)` | `Core.removeType/renameType`（用类型名） | `TagServe::removeType/renameType` + `saveTag` |
| `addTag(typeId,n)` / `removeTag(id)` | `Core.addTag/removeTag` | `TagServe::addTag/removeTag` + `saveTag` |
| `typeColor` | 纯 UI 态（选色盘） | `Core.setTypeColor` 已暴露，页面当前没调用 |
| `toast(m)` | 本仓 `toastRequested` | `Core.errorOccurred` / `infoMessage` → toast |
| `toggleServer()` / `serverRunning` | `Core.startServer/stopServer` | `SyncServer::start(name,0,ec,cb)` / `stop` |
| `currentPage`/`selectedTypeId`/`selectedTagId`/`dirChoices` | 纯 UI 态 | — |
| 新增：`convertMode()` | `Core.convertMode` | `TagServe::convertMode(from,to,false)` + 记忆 `config.json` 的 `TagMode` |
| 新增：`refreshIndex()`/`clearInvalidData()` | `Core.refreshAll/clearInvalidData` | `TagServe::reLoadRoot(全部根)`（clearAll + 重扫，等价"重建索引/清失效"） |
| 新增：`refreshDir(i)` | `Core.refreshRoot` | `TagServe::reLoadRoot(单目录)`（单目录刷新，不整库重建） |
| 新增：`exportLibrary`/`importLibrary` | 同名 Core 方法 | `getTagPath()`+`QFile::copy` / `TagServe::mergeTags` |

`nextTypeId()/nextTagId()` 是旧 mock 的内部辅助，**没有任何页面调用**，已删除（id 现在由桥稳定分配）。

## 3. 桩 / 未接的部分

- **文件/目录选择器没接**：页面调的是 `Store.addFile("","")`、`Store.addDir("","")`，真实现里"添加"必须有真实路径，所以这两个入口目前只弹提示；临时可用 `Store.addDir("名字","D:/某目录")`。要真正可用，得在页面挂 `FolderDialog`（页面不许改，所以留给用户决定）。
- **页面级 mock 按钮**（不是桥的桩，是页面直接 `Store.toast(...)`）：PageDirs 的"刷新/打开"、PageBrowse 的"搜索"、PageSettings 的"模式转换/刷新索引/清除失效/导出/导入"、PageSync 的队列与服务端列表。对应能力都已在 `Core`/`Store` 上备好（见上表"新增"）。
- **同步**：`SyncServer` 是真实 start/stop（广播 + 收连接），但**任务队列没接**（没人 `enqueueDirectory`，`Core.enqueueDirectory` 已提供）；**SyncClient 完全未接**。
- `selectedTagId`/`typeColor` 的"改颜色"只对新类型生效：页面选色后只改 `Store.typeColor`，没调 `setTypeColor`。
- 桥另外提供 `Core.pathAt(index)`（第 index 行的真实路径）与 `Core.enqueueDirectory(path)`，Store 侧对应 `Store.pathOf(index)`，留给后续接同步/调试用。

## 4. 路径与编码约定

- 配置目录 `./config/`（相对进程工作目录，沿用旧 windows 工程）：`path.json`（受管目录白名单）、`tag.json`（标签库）、`index.db`（索引库）、`config.json`（只用 `TagMode` 字段记忆默认存储模式，其它字段原样保留）。`main.cpp` 会先 `mkpath("config")`（`TagLibrary` 保存时不建父目录）。
- 路径**一律绝对路径 + 正斜杠**（core 内部 `generic_u8string()`）；QML 传进来的 `\` 会被换成 `/`；`files[].dirPath` 只在显示时用 `QDir::toNativeSeparators()` 转回系统分隔符。
- 字符串一律 UTF-8：`QString::toUtf8()` / `QString::fromUtf8()`，core 侧 `std::filesystem::u8path`。
- `QML 字符串 -> 文件路径` 统一走 `pathFromQString()`，`core 路径 -> QML` 走 `pathToQString()`。
- 搜索的目录过滤（`SearchOptions::dirs_`）在 core 里是**字节前缀范围比较**，所以必须传 `DirectoryConfigManager` 的 canonical 写法；手写 `path.json` 时大小写/分隔符不一致会导致"目录页有文件但按目录筛选查不到"（core 现状）。

## 5. 已知风险 / 需要你定的事

1. **哨兵技巧**：core 没有"列出全部文件"的接口（三容器全空 = 只返回没有标签的文件）。桥内部用 `exclude_ = ["\001__tagmeow_list_all__"]` 全量取数来算 `totalFileCount` 与每个目录的文件数；Store 侧"一个目录都不勾选"用 `"\u0001__tagmeow_no_dir__"` 表达。core 若改了空容器语义，这两处要跟着改。
2. **目录行被过滤**：`FileDatabase::insertDirectory` 会把子目录也写成记录，搜索会返回目录行。桥用 `QFileInfo::isDir()` 过滤掉（每行一次 stat）。代价见第 4 条。
3. **计数口径**：`Store.fileCount()` = 当前列表长度（与 PageBrowse 的 "N 项" 一致），库内总数是新增的 `Store.totalFileCount()`。Main.qml 顶栏/侧栏用的是 `fileCount()`——想让它显示库内总数就把那两处换成 `totalFileCount()`（要改 Main.qml，交给你定）。
4. **默认视图 = 没有标签的文件**（core/android 语义 = 待整理收件箱）。所以索引完一批已带标签的文件时，浏览页可能只有很少几行。
5. **性能**：每次查询/重建目录都会对每行做一次 `stat`（大小、时间、目录判定），全表同步执行在 UI 线程；十万级库初次进入会明显卡。后续建议把重操作挪到工作线程（core 自身线程安全）。
6. **Filename 模式会改名**：加标签后文件真实路径变了，列表刷新后行号也会变（列表按路径排序）；`Store` 里一律用 `files[i].path` 反查，不要用文件名拼接。
7. `removeFile` 只删索引记录，磁盘文件保留（页面上的 ✕ 语义）——已改成这种提示文案。
8. **未编译验证**（按你的要求没编）。`qmlcachegen` 可能对 `import Core 1.0`（C++ 运行期注册的模块）报"找不到模块"的**警告**，运行期正常；若在你的工具链上报成错误，改用 `QML_ELEMENT` + 把 `corebridge.h` 放进 `qt_add_qml_module(... SOURCES ...)` 注册进 `windows` 模块。
9. 服务端 start 用 `config.json` 的 `BroadcastPort`（默认 11451），端口被占会 toast 失败。
10. `tags` 里每个 chip 的 `tone`（blue/green/plain）是按**类型名哈希**稳定映射的——Chip.qml 没有按类型色渲染的入口，所以这里只能给三套配色。
