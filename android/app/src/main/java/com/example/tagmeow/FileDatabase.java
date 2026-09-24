package com.example.tagmeow;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.TreeSet;

// 本地 SQLite 文件索引
// 主职责：
// - 自己遍历受管理目录
// - 获取文件基础信息
// - 通过 TagExtractor 获取文件标签
// - 建立 / 更新 SQLite 索引
// - 按标签查询
// 只修改数据库 不修改真实磁盘文件
// 自己调用时保证线程安全

// struct FileInfo
// {
//     int file_id_;                   // 自增主键
//     std::string path_;              // 绝对路径 UTF-8
//     std::string rel_path_;          // 相对路径 UTF-8
//     std::vector<std::string> tags_; // 标签列表
//     int file_version_;              // 乐观锁版本
// };
// mtime / size / sidecar_mtime / last_refresh_time 不再入库：
// 数据库只作磁盘的缓存 这些随时会变的值按需现取（查询出结果时由 readFileInfo 读一次磁盘）

public final class FileDatabase {

    // 数据库结构版本
    // - v3：原版表结构 + root_id + original_uri
    // - v4：增加 is_dir（文件浏览器需要区分目录与文件）
    // - v5：砍掉 mtime / size / sidecar_mtime / last_refresh_time（数据库只作磁盘的缓存）
    //       老库不迁移 升级时直接重建（重建后重扫一次索引就回来了）
    private static final int DB_VERSION = 5;

    // 文件索引记录
    public static final class FileInfo {

        // SQLite 自增主键
        public long file_id;
        // root_id + relativ_path
        public FileRef file_ref;
        // 当前文件对应的 Android Uri 字符串 逻辑身份不依赖于它
        public String original_uri;
        // 是否为目录 文件浏览器需要用它区分图标与行为
        public boolean is_directory;
        // 文件修改时间：不落库 查询时按磁盘现取（界面显示大小 / 缩略图缓存键要用）
        public long file_mtime;
        // 文件大小：不落库 同上
        public long file_size;
        // 文件标签列表
        public List<String> tags = new ArrayList<>();
        // 乐观锁版本
        public long file_version;

        @Override
        public String toString() {
            return (file_ref == null ? "<none>" : file_ref.toString()) + " " + tags;
        }
    }

    // Tag 与 File 的关联记录
    public static final class TagEntry {

        // 标签名称
        public String tag;
        // 关联的文件 ID
        public long file_id;

        public TagEntry() {
        }

        public TagEntry(String tag, long file_id) {
            this.tag = tag;
            this.file_id = file_id;
        }
    }

    // 搜索条件
    public static final class SearchOptions {

        // 排除包含这些标签的文件
        public List<String> exclude = new ArrayList<>();
        // 文件只能包含这些标签
        public List<String> only = new ArrayList<>();
        // 至少包含其中一个标签
        public List<String> include = new ArrayList<>();
    }

    // SQLiteOpenHelper 封装
    private static final class Helper extends SQLiteOpenHelper {

        Helper(Context context, String path) {
            super(context, path, null, DB_VERSION);
            setWriteAheadLoggingEnabled(true);
        }

        @Override
        public void onConfigure(SQLiteDatabase db) {
            db.setForeignKeyConstraintsEnabled(true);
        }

        @Override
        public void onCreate(SQLiteDatabase db) {
            createSchema(db);
        }

        @Override
        public void onUpgrade(SQLiteDatabase db, int old_version, int new_version) {
            // 老库直接重建：数据库只是磁盘的缓存 重建之后重扫一次就有内容
            // （字段砍掉之后 以前那条逐版本加列的迁移链没有意义了）
            dropSchema(db);
            createSchema(db);
        }

        @Override
        public void onOpen(SQLiteDatabase db) {
            super.onOpen(db);

            // 索引补齐（幂等）：老库没建过 tags(file_id) 就在这里补上
            // 新库的建表语句里已经有了 这句对它是空操作
            db.execSQL("CREATE INDEX IF NOT EXISTS idx_tag_file ON tags(file_id);");
        }

        @Override
        public void onDowngrade(SQLiteDatabase db, int old_version, int new_version) {
            // 降级时结构不保证兼容 直接重建
            dropSchema(db);
            createSchema(db);
        }

        private static void dropSchema(SQLiteDatabase db) {
            db.execSQL("DROP TABLE IF EXISTS tags;");
            db.execSQL("DROP TABLE IF EXISTS files;");
        }

        private static void createSchema(SQLiteDatabase db) {
            db.execSQL(
                    "CREATE TABLE IF NOT EXISTS files ("
                            + "file_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            + "path TEXT UNIQUE NOT NULL,"
                            + "rel_path TEXT,"
                            + "root_id TEXT,"
                            + "original_uri TEXT,"
                            + "is_dir INTEGER DEFAULT 0,"
                            + "file_version INTEGER DEFAULT 1"
                            + ");");

            db.execSQL("CREATE INDEX IF NOT EXISTS idx_path ON files(path);");
            db.execSQL("CREATE INDEX IF NOT EXISTS idx_root ON files(root_id);");

            db.execSQL(
                    "CREATE TABLE IF NOT EXISTS tags ("
                            + "tag TEXT NOT NULL,"
                            + "file_id INTEGER NOT NULL,"
                            + "PRIMARY KEY (tag, file_id),"
                            + "FOREIGN KEY (file_id) REFERENCES files(file_id) ON DELETE CASCADE"
                            + ");");

            db.execSQL("CREATE INDEX IF NOT EXISTS idx_tag ON tags(tag);");
            // 按 file_id 关联/删除也要走索引：缺它时 listDirectory / searchByTags 的
            // LEFT JOIN tags 以及删文件时的级联都会退化成全表扫 tags
            db.execSQL("CREATE INDEX IF NOT EXISTS idx_tag_file ON tags(file_id);");
        }
    }

    // 查询用的列顺序
    private static final String[] FILE_COLUMNS = {
            "f.file_id",
            "f.rel_path",
            "f.root_id",
            "f.original_uri",
            "f.file_version",
            "f.is_dir",
    };

    // 查询结果中 tags 列的索引
    private static final int TAGS_COLUMN = FILE_COLUMNS.length;
    // 查询结果中 tags 列的索引
    private final Context context;
    // 数据库文件
    private final File db_file;
    //  StorageAccess 用于访问文件系统
    private final StorageAccess storage;
    // SQLiteOpenHelper 封装
    private Helper helper;
    // 最后一次错误信息
    private String error_string = "";

    public FileDatabase(Context context, File db_file, StorageAccess storage) {

        this.context = Objects.requireNonNull(context);
        this.db_file = Objects.requireNonNull(db_file);
        this.storage = Objects.requireNonNull(storage);
    }

    // 重新加载数据库
    public boolean reload() {
        close();

        File parent = db_file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            error_string = "[warning] Cannot create database directory: " + parent;
            return false;
        }

        try {
            helper = new Helper(context, db_file.getAbsolutePath());
            helper.getWritableDatabase();
        } catch (RuntimeException error) {
            helper = null;
            error_string = "[warning] Failed to open database: " + error.getMessage();
            return false;
        }

        return initSchema();
    }

    // 初始化数据库结构
    public boolean initSchema() {
        if (helper == null && !reload()) {
            return false;
        }

        try {
            helper.getWritableDatabase();
        } catch (RuntimeException error) {
            error_string = "[warning] Database not opened: " + error.getMessage();
            return false;
        }

        error_string = "";
        return true;
    }

    // 关闭数据库
    public void close() {
        if (helper != null) {
            helper.close();
            helper = null;
        }
    }

    private SQLiteDatabase db() {
        if (helper == null) {
            reload();
        }

        return helper == null ? null : helper.getWritableDatabase();
    }

    // 遍历整个 root 并同步数据库 FileDatabase 自己负责遍历 标签通过 TagExtractor 获取
    // 清空全部文件/标签记录：数据库只作磁盘的缓存 旧数据不需要保留
    // 与 core 的 FileDatabase::clearAll 对应（先删关联表 不依赖外键级联）
    public boolean clearAll() {
        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        database.beginTransaction();
        try {
            database.delete("tags", null, null);
            database.delete("files", null, null);
            database.setTransactionSuccessful();
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        } finally {
            database.endTransaction();
        }

        error_string = "";
        return true;
    }

    // 按磁盘内容把目录写入数据库：只插入 不做存在性检查 / 去重 / 清理 / 乐观锁
    // 刷新流程是「先 clearAll 再按磁盘重扫重建」 所以这里不需要查旧行
    // 与 core 的 FileDatabase::insertDirectory 对应
    public boolean insertDirectory(FileRef root, TagExtractor extractor) {

        Objects.requireNonNull(root);
        Objects.requireNonNull(extractor);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        if (!storage.exists(root) || !storage.isDirectory(root)) {
            error_string = "[warning] Directory does not exist or not a directory";
            return false;
        }

        List<FileRef> found = new ArrayList<>();
        int skipped_dirs;

        try {
            skipped_dirs = collect(root, found, true);
        } catch (IOException error) {
            // root 本身读不了（授权被回收 / 目录被删）必须失败
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        database.beginTransaction();
        try {
            int skipped = 0;

            for (FileRef ref : found) {
                FileInfo info = buildInfo(ref, extractor);
                if (info == null) {
                    skipped++;
                    continue;
                }

                if (!insertFileRow(database, info)) {
                    return false;
                }
            }

            database.setTransactionSuccessful();

            if (skipped > 0 || skipped_dirs > 0) {
                error_string = "[tip] Inserted directory, but " + skipped
                        + " file(s) and " + skipped_dirs
                        + " subdirector(y/ies) could not be read";
            } else {
                error_string = "";
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        } finally {
            database.endTransaction();
        }

        return true;
    }

    // 只写不查的一行：不查旧 file_id / 版本号 也不先删标签
    private boolean insertFileRow(SQLiteDatabase database, FileInfo info) {

        ContentValues values = new ContentValues();
        values.put("path", info.file_ref.key());
        values.put("rel_path", info.file_ref.getRelativePath());
        values.put("root_id", info.file_ref.getRootId());
        values.put("original_uri", info.original_uri);
        values.put("is_dir", info.is_directory ? 1 : 0);
        values.put("file_version", info.file_version <= 0 ? 1L : info.file_version);

        long file_id = database.insertWithOnConflict(
                "files",
                null,
                values,
                SQLiteDatabase.CONFLICT_REPLACE);

        if (file_id < 0) {
            error_string = "[warning] Failed to insert file record: " + info.file_ref;
            return false;
        }

        if (info.tags == null) {
            return true;
        }

        for (String tag : info.tags) {
            if (tag == null || tag.isEmpty()) {
                continue;
            }

            ContentValues tag_values = new ContentValues();
            tag_values.put("tag", tag);
            tag_values.put("file_id", file_id);

            long row_id = database.insertWithOnConflict(
                    "tags",
                    null,
                    tag_values,
                    SQLiteDatabase.CONFLICT_REPLACE);

            if (row_id < 0) {
                error_string = "[warning] Failed to insert tag: " + tag;
                return false;
            }
        }

        return true;
    }

    // 更新单个文件 文件基础信息由 FileDatabase 获取 标签通过 TagExtractor 获取
    public boolean updateFile(FileRef file, TagExtractor extractor) {

        Objects.requireNonNull(file);
        Objects.requireNonNull(extractor);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        FileInfo info = buildInfo(file, extractor);
        if (info == null) {
            error_string = "[warning] Path does not exist or is not a regular file/directory: " + file;
            return false;
        }

        database.beginTransaction();
        try {
            if (!updateFileRow(database, info)) {
                return false;
            }

            database.setTransactionSuccessful();
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        } finally {
            database.endTransaction();
        }

        error_string = "";
        return true;
    }

    // 直接使用已经获取好的 FileInfo 更新数据库
    public boolean updateFile(FileInfo info) {
        Objects.requireNonNull(info);
        Objects.requireNonNull(info.file_ref);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        database.beginTransaction();
        try {
            if (!updateFileRow(database, info)) {
                return false;
            }

            database.setTransactionSuccessful();
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        } finally {
            database.endTransaction();
        }

        error_string = "";
        return true;
    }

    // 只删除数据库中的文件记录
    public boolean removeFile(FileRef file) {
        Objects.requireNonNull(file);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        try {
            database.delete(
                    "files",
                    "path = ?",
                    new String[]{file.key()});
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        error_string = "";
        return true;
    }

    // 只删除指定 root 下的数据库记录
    public boolean removeDirectory(FileRef root) {
        Objects.requireNonNull(root);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        try {
            if (root.getRelativePath().isEmpty()) {
                database.delete("files", "root_id = ?", new String[]{root.getRootId()});
            } else {
                database.delete(
                        "files",
                        "root_id = ? AND (rel_path = ? OR substr(rel_path, 1, length(?)) = ?)",
                        new String[]{
                                root.getRootId(),
                                root.getRelativePath(),
                                root.getRelativePath() + "/",
                                root.getRelativePath() + "/",
                        });
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        error_string = "";
        return true;
    }

    // 清掉不属于当前配置的索引行
    // index.db 是两种存储方式共用的：
    // - SAF 模式的 root_id 是 tree document id（比如 primary:临时）
    // - 「所有文件访问」模式的 root_id 是绝对路径（比如 /storage/emulated/0/下载）
    // 切模式 / 删目录之后，不在当前配置里的 root 的行会一直留在库里：
    // 首页「文件 N」虚高（实测两种模式 + 已删目录加起来 125），搜索还会串模式
    // keep_root_ids：当前配置里所有目录的 id（有效的和「授权丢了」的都要留）
    public boolean purgeForeignRoots(List<String> keep_root_ids) {
        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        try {
            if (keep_root_ids == null || keep_root_ids.isEmpty()) {
                // 配置里一个目录都没有：整张表都是残留
                // （调用方在清空后会按需重扫，所以不会出现索引凭空为空）
                database.delete("files", null, null);
            } else {
                StringBuilder placeholders = new StringBuilder();
                String[] args = new String[keep_root_ids.size()];

                for (int i = 0; i < keep_root_ids.size(); i++) {
                    if (i > 0) {
                        placeholders.append(",");
                    }

                    placeholders.append("?");
                    args[i] = keep_root_ids.get(i);
                }

                database.delete(
                        "files",
                        "root_id IS NULL OR root_id = '' OR root_id NOT IN (" + placeholders + ")",
                        args);
            }

            // 顺手清掉没有归属的标签关联（不依赖外键级联有没有打开）
            database.execSQL("DELETE FROM tags WHERE file_id NOT IN (SELECT file_id FROM files);");
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        error_string = "";
        return true;
    }

    // 清理重复数据库记录
    public boolean clearRepeat() {
        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        try {
            database.execSQL(
                    "DELETE FROM files WHERE file_id NOT IN ("
                            + "SELECT MIN(file_id) FROM files GROUP BY REPLACE(path, '\\', '/'));");
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        error_string = "";
        return true;
    }

    // 清理数据库中已经不存在的文件记录 不修改真实磁盘文件
    public boolean cleanupInvalid() {
        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return false;
        }

        List<String> invalid_keys = new ArrayList<>();

        try (Cursor cursor = database.rawQuery(
                "SELECT root_id, rel_path FROM files;",
                null)) {

            while (cursor.moveToNext()) {
                String root_id = cursor.isNull(0) ? "" : cursor.getString(0);
                String rel_path = cursor.isNull(1) ? "" : cursor.getString(1);

                FileRef ref = new FileRef(root_id, rel_path);
                if (!storage.exists(ref)) {
                    invalid_keys.add(ref.key());
                }
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        if (invalid_keys.isEmpty()) {
            error_string = "";
            return true;
        }

        database.beginTransaction();
        try {
            for (String key : invalid_keys) {
                database.delete("files", "path = ?", new String[]{key});
            }

            database.setTransactionSuccessful();
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        } finally {
            database.endTransaction();
        }

        error_string = "";
        return true;
    }

    // 按标签搜索文件
    public List<FileInfo> searchByTags(SearchOptions options) {
        List<FileInfo> results = new ArrayList<>();

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return results;
        }

        SearchOptions opts = options == null ? new SearchOptions() : options;

        StringBuilder sql = new StringBuilder();
        sql.append("SELECT ")
                .append(String.join(", ", FILE_COLUMNS))
                .append(", GROUP_CONCAT(t.tag, ',') AS tags_csv")
                .append(" FROM files f LEFT JOIN tags t ON f.file_id = t.file_id")
                .append(" WHERE 1=1");

        List<String> bind_values = new ArrayList<>();

        // only：文件标签集合必须恰好等于 only 集合
        TreeSet<String> only_set = new TreeSet<>();
        for (String tag : safeList(opts.only)) {
            if (!tag.isEmpty()) {
                only_set.add(tag);
            }
        }

        if (!only_set.isEmpty()) {
            for (int i = 0; i < only_set.size(); i++) {
                sql.append(" AND EXISTS (SELECT 1 FROM tags t2 WHERE t2.file_id = f.file_id AND t2.tag = ?)");
            }

            for (String tag : only_set) {
                bind_values.add(tag);
            }

            sql.append(" AND NOT EXISTS (SELECT 1 FROM tags t2 WHERE t2.file_id = f.file_id AND t2.tag NOT IN (");
            for (int i = 0; i < only_set.size(); i++) {
                if (i > 0) {
                    sql.append(", ");
                }
                sql.append("?");
            }
            sql.append("))");

            for (String tag : only_set) {
                bind_values.add(tag);
            }
        }

        // exclude：不能包含任何 exclude 标签
        for (String tag : safeList(opts.exclude)) {
            if (tag.isEmpty()) {
                continue;
            }

            sql.append(" AND NOT EXISTS (SELECT 1 FROM tags t3 WHERE t3.file_id = f.file_id AND t3.tag = ?)");
            bind_values.add(tag);
        }

        // include：至少包含一个 include 标签
        List<String> valid_include = new ArrayList<>();
        for (String tag : safeList(opts.include)) {
            if (!tag.isEmpty()) {
                valid_include.add(tag);
            }
        }

        if (!valid_include.isEmpty()) {
            sql.append(" AND (");
            for (int i = 0; i < valid_include.size(); i++) {
                if (i > 0) {
                    sql.append(" OR ");
                }
                sql.append("EXISTS (SELECT 1 FROM tags t4 WHERE t4.file_id = f.file_id AND t4.tag = ?)");
                bind_values.add(valid_include.get(i));
            }
            sql.append(")");
        }

        sql.append(" GROUP BY f.file_id;");

        try (Cursor cursor = database.rawQuery(sql.toString(), bind_values.toArray(new String[0]))) {
            while (cursor.moveToNext()) {
                FileInfo info = readFileInfo(cursor);
                parseTags(
                        cursor.isNull(TAGS_COLUMN) ? null : cursor.getString(TAGS_COLUMN),
                        info.tags);
                results.add(info);
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return results;
        }

        error_string = "";
        return results;
    }

    // 获取单个文件信息
    public Optional<FileInfo> getFileInfo(FileRef file) {
        Objects.requireNonNull(file);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return Optional.empty();
        }

        String sql = "SELECT " + String.join(", ", FILE_COLUMNS)
                + ", GROUP_CONCAT(t.tag, ',') AS tags_csv"
                + " FROM files f LEFT JOIN tags t ON f.file_id = t.file_id"
                + " WHERE f.path = ? GROUP BY f.file_id;";

        try (Cursor cursor = database.rawQuery(sql, new String[]{file.key()})) {
            if (!cursor.moveToFirst()) {
                error_string = "";
                return Optional.empty();
            }

            FileInfo info = readFileInfo(cursor);
            parseTags(
                    cursor.isNull(TAGS_COLUMN) ? null : cursor.getString(TAGS_COLUMN),
                    info.tags);

            error_string = "";
            return Optional.of(info);
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return Optional.empty();
        }
    }

    // 列出某个目录的直接子项 目录排在前面 然后按相对路径排序
    public List<FileInfo> listDirectory(FileRef directory) {
        List<FileInfo> results = new ArrayList<>();

        Objects.requireNonNull(directory);

        SQLiteDatabase database = db();
        if (database == null) {
            error_string = "[warning] Database not opened";
            return results;
        }

        String relative = directory.getRelativePath();
        String prefix = relative.isEmpty() ? "" : relative + "/";
        int prefix_length = prefix.length();

        String sql = "SELECT " + String.join(", ", FILE_COLUMNS)
                + ", GROUP_CONCAT(t.tag, ',') AS tags_csv"
                + " FROM files f LEFT JOIN tags t ON f.file_id = t.file_id"
                + " WHERE f.root_id = ?"
                + " AND substr(f.rel_path, 1, ?) = ?"
                + " AND instr(substr(f.rel_path, ?), '/') = 0"
                + " GROUP BY f.file_id"
                + " ORDER BY f.is_dir DESC, f.rel_path ASC;";

        String[] args = {
                directory.getRootId(),
                String.valueOf(prefix_length),
                prefix,
                String.valueOf(prefix_length + 1),
        };

        try (Cursor cursor = database.rawQuery(sql, args)) {
            while (cursor.moveToNext()) {
                FileInfo info = readFileInfo(cursor);
                parseTags(
                        cursor.isNull(TAGS_COLUMN) ? null : cursor.getString(TAGS_COLUMN),
                        info.tags);
                results.add(info);
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return results;
        }

        error_string = "";
        return results;
    }

    // 统计当前索引中的文件数量
    public long countFiles() {
        SQLiteDatabase database = db();
        if (database == null) {
            return 0L;
        }

        try (Cursor cursor = database.rawQuery("SELECT COUNT(*) FROM files;", null)) {
            return cursor.moveToFirst() ? cursor.getLong(0) : 0L;
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return 0L;
        }
    }

    // 获取数据库文件
    public File getDbFile() {
        return db_file;
    }

    // 返回最后一次错误
    public String getLastError() {
        return error_string;
    }

    // 递归收集 root 下的所有条目 返回读不出来的子目录个数
    // root 层读不出来直接抛给调用方（整次扫描失败）
    // 子目录读不出来只跳过：有的 provider 下 Android/data 这类目录本来就列不出来，
    // 不能因为一个子目录让整个 root 加不进来
    private int collect(FileRef directory, List<FileRef> out, boolean root_level) throws IOException {

        List<FileRef> children;

        try {
            children = storage.listChildren(directory);
        } catch (IOException error) {
            if (root_level) {
                throw error;
            }

            return 1;
        }

        int failed = 0;

        for (FileRef child : children) {
            if (storage.isDirectory(child)) {
                if (TagFileManager.TAG_DIRECTORY.equals(child.getName())) {
                    continue;
                }

                out.add(child);
                failed += collect(child, out, false);
            } else {
                out.add(child);
            }
        }

        return failed;
    }

    // 由存储层读取元信息并生成 FileInfo 读取失败返回 null
    private FileInfo buildInfo(FileRef ref, TagExtractor extractor) {

        if (!storage.exists(ref)) {
            return null;
        }

        boolean directory = storage.isDirectory(ref);

        FileInfo info = new FileInfo();
        info.file_ref = ref;
        info.original_uri = storage.locatorOf(ref);
        info.is_directory = directory;
        // mtime / size 只给界面用 不入库
        info.file_mtime = storage.lastModified(ref);
        info.file_size = directory ? 0L : storage.size(ref);
        info.file_version = 1;
        info.tags = new ArrayList<>(safeList(extractor.extract(ref)));

        return info;
    }

    // 写入或更新一行 返回 false 表示乐观锁冲突或数据库错误
    private boolean updateFileRow(SQLiteDatabase database, FileInfo info) {

        String key = info.file_ref.key();
        long existing_id = 0L;
        long existing_version = 0L;

        try (Cursor cursor = database.rawQuery(
                "SELECT file_id, file_version FROM files WHERE path = ?;",
                new String[]{key})) {

            if (cursor.moveToFirst()) {
                existing_id = cursor.getLong(0);
                existing_version = cursor.getLong(1);
            }
        } catch (RuntimeException error) {
            error_string = "[warning] " + error.getMessage();
            return false;
        }

        long file_version = info.file_version <= 0 ? 1L : info.file_version;

        if (existing_id == 0L) {
            ContentValues values = new ContentValues();
            values.put("path", key);
            values.put("rel_path", info.file_ref.getRelativePath());
            values.put("root_id", info.file_ref.getRootId());
            values.put("original_uri", info.original_uri);
            values.put("is_dir", info.is_directory ? 1 : 0);
            values.put("file_version", file_version);

            long id = database.insert("files", null, values);
            if (id < 0) {
                error_string = "[warning] Failed to insert file record: " + info.file_ref;
                return false;
            }

            existing_id = id;
        } else {
            ContentValues values = new ContentValues();
            values.put("rel_path", info.file_ref.getRelativePath());
            values.put("root_id", info.file_ref.getRootId());
            values.put("original_uri", info.original_uri);
            values.put("is_dir", info.is_directory ? 1 : 0);
            values.put("file_version", existing_version + 1);

            int changed = database.update(
                    "files",
                    values,
                    "file_id = ? AND file_version = ?",
                    new String[]{String.valueOf(existing_id), String.valueOf(existing_version)});

            if (changed == 0) {
                error_string = "[warning] Optimistic lock conflict: file version changed";
                return false;
            }
        }

        // 维护 tags 表：先删旧关联 再插新关联
        database.delete("tags", "file_id = ?", new String[]{String.valueOf(existing_id)});

        if (info.tags != null) {
            for (String tag : info.tags) {
                if (tag == null || tag.isEmpty()) {
                    continue;
                }

                ContentValues tag_values = new ContentValues();
                tag_values.put("tag", tag);
                tag_values.put("file_id", existing_id);

                long row_id = database.insertWithOnConflict(
                        "tags",
                        null,
                        tag_values,
                        SQLiteDatabase.CONFLICT_REPLACE);

                if (row_id < 0) {
                    error_string = "[warning] Failed to insert tag: " + tag;
                    return false;
                }
            }
        }

        return true;
    }

    private FileInfo readFileInfo(Cursor cursor) {
        FileInfo info = new FileInfo();

        info.file_id = cursor.getLong(0);

        String rel_path = cursor.isNull(1) ? "" : cursor.getString(1);
        String root_id = cursor.isNull(2) ? "" : cursor.getString(2);

        info.file_ref = new FileRef(root_id, rel_path);
        info.original_uri = cursor.isNull(3) ? null : cursor.getString(3);
        info.file_version = cursor.isNull(4) ? 0L : cursor.getLong(4);
        info.is_directory = !cursor.isNull(5) && cursor.getInt(5) != 0;

        // mtime / size 不在库里：查出来的每一行都按磁盘现取一次
        // （只有列表 / 搜索 / 单条查询会走到这里 扫描入库用的是 buildInfo）
        if (info.file_ref != null) {
            info.file_mtime = storage.lastModified(info.file_ref);

            if (!info.is_directory) {
                info.file_size = storage.size(info.file_ref);
            }
        }

        return info;
    }

    // 解析 GROUP_CONCAT 得到的标签串
    private static void parseTags(String csv, List<String> out) {

        if (csv == null || csv.isEmpty()) {
            return;
        }

        for (String tag : csv.split(",")) {
            if (!tag.isEmpty()) {
                out.add(tag);
            }
        }
    }

    private static List<String> safeList(List<String> list) {
        return list == null ? new ArrayList<>() : list;
    }
}