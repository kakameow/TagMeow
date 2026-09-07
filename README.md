# TagMeow

基于 **Qt Quick (Qt 6)** 的文件标签管理工具，配套 C++ (core) 局域网文件同步功能。

## 功能简介

- **目录管理**：添加/移除授权目录，自动扫描目录树并同步到本地数据库
- **标签库**：类型 + 标签两级结构，每类一个颜色；支持添加/删除/改色，操作即时保存
- **文件打标签**：文件与文件夹均可打标签（sidecar JSON 存储），按行展示并实时编辑
- **标签搜索**：包含 / 排除 / 只有 三种条件组合，实时计数（文件数 / 标签数）
- **文件定位**：双击文件行在系统资源管理器中打开所在目录并高亮，双击目录行直接打开
- **局域网同步**：UDP 广播发现设备，TCP 传输；本机分享目录 / 从其他设备下载 / 下载记录
- **设置**：语言切换（zh-cn / en-us，扫描语言目录动态列出）、存储模式转换（sidecar ↔ 文件名）
- 浅色主题界面

## 目录结构

```
git/
├── core/      # 核心库（标签/数据库/目录/局域网同步）
├── test.cpp   # 命令行测试入口 (test.cpp)
└── windows/   # Qt Quick GUI (CMake)
```

## 构建 (Windows)

- 需要 Qt 6.10+（MinGW）与 CMake + Ninja
- 在 `git/windows` 下配置并构建 `TagMeow` 目标
- 运行目录需要 `language/`（界面语言文件，构建后自动复制）与 `config/`（tag.json / index.db / path.json）

## 第三方库与素材

- [sqlite-amalgamation-3530400](https://www.sqlite.org/download.html) — 本地数据库
- [nlohmann/json](https://github.com/nlohmann/json) — JSON 解析
- [asio 1.38.2](https://think-async.com/Asio/) — 网络 I/O
- 图片资源来自 [Lucide](https://lucide.dev/license)（IS 许可证）
