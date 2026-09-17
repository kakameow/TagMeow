package com.example.tagmeow;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;

// 标签模块统一业务入口
// 协调：
// - DirectoryConfigManager
// - TagLibrary
// - TagFileManager
// - FileDatabase
// 正常情况下 UI / ViewModel / Service
// 只通过 TagServe 访问标签模块

// 构造函数会读取目录配置、标签库并初始化数据库
// 标签库的修改需要在合适时机调用 saveTag() 持久化

public final class TagServe {

    // 目录配置管理器
    private final DirectoryConfigManager directory_manager;
    // 标签库
    private final TagLibrary tag_library;
    // 文件标签管理器
    private final TagFileManager tag_file_manager;
    // 文件数据库
    private final FileDatabase file_database;
    // 存储层 用于注册/注销管理根
    private final StorageAccess storage;
    // TagServe 对外保证自身线程安全
    private final Object lock = new Object();
    // 最后一次业务错误
    private String error_string = "";

    public TagServe(Context context, File dir_config_file, File tag_lib_file, File db_file, StorageAccess storage, StoreMode default_mode) {

        Objects.requireNonNull(context);
        Objects.requireNonNull(dir_config_file);
        Objects.requireNonNull(tag_lib_file);
        Objects.requireNonNull(db_file);

        this.storage = Objects.requireNonNull(storage);
        this.directory_manager = new DirectoryConfigManager(dir_config_file, this::validateDirectory);
        this.tag_library = new TagLibrary(tag_lib_file);
        this.tag_file_manager = new TagFileManager(storage, default_mode == null ? StoreMode.SIDECAR : default_mode);
        this.file_database = new FileDatabase(context, db_file, storage);
        this.file_database.initSchema();
    }

    // 添加新的 SAF Tree Uri
    public boolean addRoot(Uri tree_uri) {
        Objects.requireNonNull(tree_uri);

        synchronized (lock) {
            if (!directory_manager.addDirectory(tree_uri)) {
                error_string = directory_manager.getLastError();
                return false;
            }

            DirectoryConfigManager.Directory directory = directory_manager.getLastValidDirectory();

            if (directory == null) {
                error_string = "[warning] no valid directory";
                return false;
            }

            storage.addRoot(directory.getId(), locatorOf(directory.getUri()));

            FileRef root = new FileRef(directory.getId(), "");

            if (!file_database.updateDirectory(root, extractor())) {
                // 扫描失败时回滚目录配置 保持配置与数据库一致
                directory_manager.removeDirectory(tree_uri);
                error_string = "Failed to update database for root: " + file_database.getLastError();
                return false;
            }

            // 配置必须真的落盘：以前这里忽略返回值，配置写失败也算成功，
            // 结果重启（或下一次 reload）之后目录就凭空消失了
            if (!directory_manager.saveToFile()) {
                file_database.removeDirectory(root);
                directory_manager.removeDirectory(directory.getId());
                storage.removeRoot(directory.getId());
                error_string = "Failed to save directory config: " + directory_manager.getLastError();
                return false;
            }

            error_string = "";
            return true;
        }
    }

    // 删除指定管理根目录
    public boolean removeRoot(Uri tree_uri) {
        Objects.requireNonNull(tree_uri);

        synchronized (lock) {
            String root_id = DirectoryConfigManager.makeRootId(tree_uri);
            DirectoryConfigManager.Directory directory = directory_manager.getDirectory(root_id);

            if (directory == null) {
                error_string = "[warning] Root not found: " + tree_uri;
                return false;
            }

            FileRef root = new FileRef(directory.getId(), "");

            if (!file_database.removeDirectory(root)) {
                error_string = "Failed to remove directory from database: " + file_database.getLastError();
                return false;
            }

            directory_manager.removeDirectory(root_id);

            if (!directory_manager.saveToFile()) {
                error_string = "Failed to save directory config: " + directory_manager.getLastError();
                return false;
            }

            storage.removeRoot(root_id);

            error_string = "";
            return true;
        }
    }

    // 根据 DirectoryConfigManager 当前配置 重新加载所有有效 root
    public boolean reLoadRoot() {
        synchronized (lock) {
            // 旧的（含已经失效的）root 记录全部先清掉
            for (DirectoryConfigManager.Directory directory : directory_manager.getDirList()) {
                file_database.removeDirectory(new FileRef(directory.getId(), ""));
            }

            directory_manager.clearInvalidPath();

            int failed = scanRoots();
            file_database.clearRepeat();
            file_database.cleanupInvalid();

            if (failed > 0) {
                error_string = "[warning] " + failed + " root(s) failed to update";
                return false;
            }

            error_string = "";
            return true;
        }
    }

    // 获取当前有效 root
    public List<DirectoryConfigManager.Directory>
    getRoots() {

        synchronized (lock) {
            return directory_manager.getValidDirList();
        }
    }

    // 加载标签库
    public boolean loadTag() {
        synchronized (lock) {
            return tagResult(tag_library.loadTagsFromFile(tag_library.getConfigFile()));
        }
    }

    // 保存标签库
    public boolean saveTag() {
        synchronized (lock) {
            return tagResult(tag_library.saveTagsToFile());
        }
    }

    // 合并标签库（导入用）：把 source_file 里的标签并进当前标签库
    // 合并成功后调用方一般还要 saveTag() 落盘
    public boolean mergeTag(File source_file) {
        synchronized (lock) {
            return tagResult(tag_library.mergeTags(source_file));
        }
    }

    // 添加 Tag
    public boolean addTag(String tag, String type) {

        synchronized (lock) {
            return tagResult(tag_library.addTag(tag, type));
        }
    }

    // 删除 Tag
    public boolean removeTag(String tag) {

        synchronized (lock) {
            return tagResult(tag_library.removeTag(tag));
        }
    }

    // 重命名 Tag
    public boolean renameTag(String old_tag, String new_tag) {

        synchronized (lock) {
            return tagResult(tag_library.renameTag(old_tag, new_tag));
        }
    }

    // 添加 Type
    public boolean addType(String type) {

        synchronized (lock) {
            return tagResult(tag_library.addType(type));
        }
    }

    // 添加 Type 并指定颜色
    public boolean addType(String type, String color) {

        synchronized (lock) {
            return tagResult(tag_library.addType(type, color));
        }
    }

    // 删除 Type
    public boolean removeType(String type) {

        synchronized (lock) {
            return tagResult(tag_library.removeType(type));
        }
    }

    // 重命名 Type
    public boolean renameType(String old_type, String new_type) {

        synchronized (lock) {
            return tagResult(tag_library.renameType(old_type, new_type));
        }
    }

    // 设置 Type 颜色
    public boolean setTypeColor(String type, String color) {

        synchronized (lock) {
            return tagResult(tag_library.setTypeColor(type, color));
        }
    }

    // 设置 Tag 所属 Type
    public boolean setTagType(String tag, String type) {

        synchronized (lock) {
            return tagResult(tag_library.setTagType(tag, type));
        }
    }

    // 判断 Tag 是否存在
    public boolean hasTag(String tag) {

        synchronized (lock) {
            return tag_library.hasTag(tag);
        }
    }

    // 判断 Type 是否存在
    public boolean hasType(String type) {

        synchronized (lock) {
            return tag_library.hasType(type);
        }
    }

    // 获取 Type 对应颜色
    public String getColorByType(String type) {

        synchronized (lock) {
            return tag_library.getColorByType(type);
        }
    }

    // 获取 Tag 所属 Type
    public String getTypeOfTag(String tag) {

        synchronized (lock) {
            return tag_library.getTypeOfTag(tag);
        }
    }

    // 获取所有 Type 名称
    public List<String> getAllTypeNames() {
        synchronized (lock) {
            return tag_library.getAllTypeNames();
        }
    }

    // 获取所有 Tag 名称
    public List<String> getAllTagNames() {
        synchronized (lock) {
            return tag_library.getAllTagNames();
        }
    }

    // 根据前缀自动补全
    public List<String> autoComplete(
            String prefix) {

        synchronized (lock) {
            return tag_library.autoComplete(prefix);
        }
    }

    // 使用 TagFileManager 默认模式添加文件标签
    // 只修改真实文件 不刷新数据库
    // 需要刷新时另外调用 updateFile()
    public boolean addFileTag(FileRef file, String tag) {

        Objects.requireNonNull(file);

        synchronized (lock) {
            if (isInsideTagDirectory(file)) {
                error_string = "[warning] the tag directory cannot recommend or empty";
                return false;
            }

            if (file.isRoot() && directory_manager.containsRoot(file.getRootId())) {
                error_string = "[warning] the root directory cannot recommend";
                return false;
            }

            if (!tag_library.hasTag(tag)) {
                error_string = "[warning] tag or path does not exist";
                return false;
            }

            boolean ok = tag_file_manager.addTag(file, tag);
            error_string = ok ? "" : tag_file_manager.getLastError();
            return ok;
        }
    }

    // 使用 TagFileManager 默认模式删除文件标签
    public boolean removeFileTag(FileRef file, String tag) {

        Objects.requireNonNull(file);

        synchronized (lock) {
            boolean ok = tag_file_manager.removeTag(file, tag);
            error_string = ok ? "" : tag_file_manager.getLastError();
            return ok;
        }
    }

    // 批量删除文件标签
    public boolean removeFileTag(FileRef file, List<String> tags) {

        Objects.requireNonNull(file);

        synchronized (lock) {
            boolean ok = tag_file_manager.removeTag(file, tags);
            error_string = ok ? "" : tag_file_manager.getLastError();
            return ok;
        }
    }

    // 从真实文件重新读取标签并更新数据库
    public boolean updateFile(FileRef file) {

        Objects.requireNonNull(file);

        synchronized (lock) {
            // Filename 模式下加/删标签会改文件名
            FileRef target = resolveWrittenPath(file);

            if (!storage.exists(target)) {
                error_string = "[warning] Path does not exist: " + file;
                return false;
            }

            boolean ok = file_database.updateFile(target, extractor());
            if (!ok) {
                error_string = file_database.getLastError();
                return false;
            }

            // 删除改名之后旧路径的记录
            if (!target.equals(file)) {
                file_database.removeFile(file);
            }

            error_string = "";
            return true;
        }
    }

    // 最后一次文件标签操作实际写入的路径
    public FileRef getLastWrittenPath() {
        synchronized (lock) {
            return tag_file_manager.getLastWrittenPath();
        }
    }

    // 请求路径已经不存在时 尝试沿最后一次写入的实际路径解析同一个文件
    // Filename 模式下同一个文件的身份 = 去掉标签块之后的路径
    private FileRef resolveWrittenPath(FileRef requested) {
        if (storage.exists(requested)) {
            return requested;
        }

        FileRef written = tag_file_manager.getLastWrittenPath();
        if (written == null || !storage.exists(written)) {
            return requested;
        }

        boolean same_file = TagFileManager.removeFilenameTagsPath(written)
                .equals(TagFileManager.removeFilenameTagsPath(requested));

        return same_file ? written : requested;
    }

    // 批量转换全部受管理文件的标签模式
    // 文件：
    // Filename / Sidecar 相互转换
    // 文件夹：
    // 跳过 文件夹固定使用 Sidecar
    public boolean convertMode(
            StoreMode from,
            StoreMode to) {

        return convertMode(from, to, false);
    }

    // 批量转换全部受管理文件的标签模式
    // keep_old 为 false：
    // 转换后删除旧模式数据
    // keep_old 为 true：
    // 保留旧模式数据
    public boolean convertMode(StoreMode from, StoreMode to, boolean keep_old) {

        Objects.requireNonNull(from);
        Objects.requireNonNull(to);

        synchronized (lock) {
            if (from == to) {
                error_string = "";
                return true;
            }

            // 第一阶段：对所有文件只写入新模式 不删除旧模式
            List<FileRef> files = collectRegularFiles();
            int failed_first = 0;
            String first_error = "";

            for (FileRef file : files) {
                if (!tag_file_manager.convertMode(file, from, to, true)) {
                    failed_first++;
                    if (failed_first == 1) {
                        first_error = "First failure in first pass: " + file
                                + " (" + tag_file_manager.getLastError() + ")";
                    }
                }
            }

            if (keep_old) {
                if (failed_first > 0) {
                    error_string = "[warning] " + failed_first
                            + " file(s) failed to write new format. " + first_error;
                    return false;
                }

                tag_file_manager.setDefaultMode(to);
                error_string = "";
                return true;
            }

            if (failed_first > 0) {
                error_string = "[warning] " + failed_first
                        + " file(s) failed in first pass (keep_old=true), aborting second pass. "
                        + first_error;
                return false;
            }

            // 第二阶段：删除旧模式数据
            int failed_second = 0;
            String second_error = "";

            for (FileRef file : files) {
                if (!tag_file_manager.removeModeTags(file, from)) {
                    failed_second++;
                    if (failed_second == 1) {
                        second_error = "First failure in second pass: " + file
                                + " (" + tag_file_manager.getLastError() + ")";
                    }
                }
            }

            if (failed_second > 0) {
                error_string = "[warning] " + failed_second
                        + " file(s) failed in second pass (keep_old=false). "
                        + "New data written, but some old data may remain. " + second_error;
                return false;
            }

            tag_file_manager.setDefaultMode(to);
            error_string = "";
            return true;
        }
    }

    // 设置以后文件标签操作的默认模式
    public void setDefaultMode(StoreMode mode) {

        synchronized (lock) {
            tag_file_manager.setDefaultMode(mode);
        }
    }

    // 获取当前默认模式
    public StoreMode getDefaultMode() {
        synchronized (lock) {
            return tag_file_manager.getDefaultMode();
        }
    }

    // 按标签搜索文件
    public List<FileDatabase.FileInfo> searchByTags(FileDatabase.SearchOptions options) {

        synchronized (lock) {
            List<FileDatabase.FileInfo> result = file_database.searchByTags(options);
            error_string = file_database.getLastError();
            return result;
        }
    }

    // 获取单个文件信息
    public Optional<FileDatabase.FileInfo> getFileInfo(FileRef file) {

        Objects.requireNonNull(file);

        synchronized (lock) {
            return file_database.getFileInfo(file);
        }
    }

    // 列出某个目录的直接子项 给 UI 的文件浏览用（原版只有按标签搜索）
    public List<FileDatabase.FileInfo> listDirectory(FileRef directory) {

        Objects.requireNonNull(directory);

        synchronized (lock) {
            List<FileDatabase.FileInfo> result = file_database.listDirectory(directory);
            error_string = file_database.getLastError();
            return result;
        }
    }

    // 获取 Type -> Tag 的只读视图 给 UI 画标签库用
    public Map<String, List<String>> getTypeTag() {
        synchronized (lock) {
            return tag_library.getTypeTag();
        }
    }

    // 获取 Type -> Color 的只读视图 给 UI 上色用
    public Map<String, String> getTypeColor() {
        synchronized (lock) {
            return tag_library.getTypeColor();
        }
    }

    // 刷新全部受管理目录
    // DirectoryConfigManager -> FileDatabase.updateDirectory() -> TagFileManager.extractTags()
    // 返回 false 时 error_string 里有警告信息
    public boolean refreshAll() {
        synchronized (lock) {
            int failed = scanRoots();
            file_database.clearRepeat();
            file_database.cleanupInvalid();

            if (failed > 0) {
                error_string = "[warning] " + failed + " root(s) failed to update";
                return false;
            }

            error_string = "";
            return true;
        }
    }

    // 刷新指定 root
    public boolean refreshRoot(String root_id) {

        synchronized (lock) {
            DirectoryConfigManager.Directory directory = directory_manager.getDirectory(root_id);

            if (directory == null) {
                error_string = "[warning] Root not found: " + root_id;
                return false;
            }

            storage.addRoot(directory.getId(), locatorOf(directory.getUri()));

            FileRef root = new FileRef(directory.getId(), "");

            if (!file_database.updateDirectory(root, extractor())) {
                error_string = "Failed to update database for root: " + file_database.getLastError();
                return false;
            }

            error_string = "";
            return true;
        }
    }

    // 获取 DirectoryConfigManager 主要供调试/测试使用
    public DirectoryConfigManager getDirectoryConfigManager() {

        return directory_manager;
    }

    // 获取 TagLibrary 主要供调试/测试使用
    public TagLibrary getTagLibrary() {

        return tag_library;
    }

    // 获取 TagFileManager 主要供调试/测试使用
    public TagFileManager getTagFileManager() {

        return tag_file_manager;
    }

    // 获取 FileDatabase 主要供调试/测试使用
    public FileDatabase getFileDatabase() {

        return file_database;
    }

    // 返回最后一次业务错误
    public String getLastError() {
        synchronized (lock) {
            return error_string;
        }
    }

    // DirectoryConfigManager 的目录有效性校验器 注册 root 后通过存储层判断是否可访问
    private boolean validateDirectory(String root_id, Uri uri) {

        storage.addRoot(root_id, locatorOf(uri));

        FileRef root = new FileRef(root_id, "");
        return storage.exists(root) && storage.isDirectory(root);
    }

    // 扫描全部有效 root 返回失败的 root 数量
    private int scanRoots() {
        int failed = 0;

        for (DirectoryConfigManager.Directory directory : directory_manager.getValidDirList()) {
            storage.addRoot(directory.getId(), locatorOf(directory.getUri()));

            FileRef root = new FileRef(directory.getId(), "");
            if (!file_database.updateDirectory(root, extractor())) {
                failed++;
            }
        }

        return failed;
    }

    // 收集全部有效 root 下的普通文件 跳过 .tag 目录
    private List<FileRef> collectRegularFiles() {
        List<FileRef> files = new ArrayList<>();

        for (DirectoryConfigManager.Directory directory : directory_manager.getValidDirList()) {
            storage.addRoot(directory.getId(), locatorOf(directory.getUri()));

            FileRef root = new FileRef(directory.getId(), "");

            if (!storage.exists(root) || !storage.isDirectory(root)) {
                continue;
            }

            collectRegularFiles(root, files);
        }

        return files;
    }

    private void collectRegularFiles(FileRef directory, List<FileRef> out) {

        for (FileRef child : storage.listChildren(directory)) {
            if (storage.isDirectory(child)) {
                if (TagFileManager.TAG_DIRECTORY.equals(child.getName())) {
                    continue;
                }

                collectRegularFiles(child, out);
            } else {
                out.add(child);
            }
        }
    }

    // 判断引用是否位于 Sidecar 存储目录内
    private static boolean isInsideTagDirectory(FileRef file) {
        String relative = file.getRelativePath();

        if (relative.isEmpty()) {
            return false;
        }

        for (String segment : relative.split("/")) {
            if (TagFileManager.TAG_DIRECTORY.equals(segment)) {
                return true;
            }
        }

        return false;
    }

    // 把 Uri 转换为存储层定位串 file Uri 使用纯路径 便于本地存储实现直接使用
    private static String locatorOf(Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            String path = uri.getPath();
            if (path != null && !path.isEmpty()) {
                return path;
            }
        }

        return uri.toString();
    }

    // 扫描时向 TagFileManager 获取标签
    private TagExtractor extractor() {
        return tag_file_manager::extractTags;
    }

    // 统一处理 TagLibrary 的返回值与错误
    private boolean tagResult(boolean ok) {
        error_string = ok ? "" : tag_library.getLastError();
        return ok;
    }
}