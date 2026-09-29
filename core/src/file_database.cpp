#include "file_database.h"

FileDatabase::FileDatabase(const std::filesystem::path &db_path_utf8) : db_path_(db_path_utf8)
{
    reload(db_path_);
}

FileDatabase::~FileDatabase()
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool FileDatabase::reload(const std::filesystem::path &db_path_utf8)
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }

    db_path_ = db_path_utf8;
    error_string_.clear();

    std::error_code ec;
    const std::filesystem::path parent = db_path_utf8.parent_path();

    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        if (ec || !std::filesystem::create_directories(parent, ec))
        {
            if (ec)
            {
                error_string_ = "[warning] Failed to create database directory: " + ec.message();
            }
            else
            {
                error_string_ = "[warning] Failed to create database directory";
            }

            return false;
        }
    }

    const std::string db_path_string = db_path_utf8.generic_u8string();
    const int result = sqlite3_open(db_path_string.c_str(), &db_);

    if (result != SQLITE_OK)
    {
        if (db_)
        {
            error_string_ = "[warning] Failed to open database: " + std::string(sqlite3_errmsg(db_));
            sqlite3_close(db_);
            db_ = nullptr;
        }
        else
        {
            error_string_ = "[warning] Failed to open database";
        }

        return false;
    }

    // 数据库只是磁盘缓存 不要求异常情况下保证事务持久性
    // journal_mode 是写在库文件里的持久属性：这里必须显式给出 否则老库(WAL)与新库(默认 DELETE)行为不一致
    sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous = OFF;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);

    if (!initSchema())
    {
        if (error_string_.empty())
        {
            error_string_ = "[warning] Database initialization failed";
        }

        return false;
    }

    error_string_.clear();
    return true;
}

bool FileDatabase::initSchema()
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    int version = 0;
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            version = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    if (version == 4)
    {
        // idx_path 与 path 的 UNIQUE 约束同列重复 批量插入时白送一倍索引维护 -> 老库顺手清掉
        sqlite3_exec(db_, "DROP INDEX IF EXISTS idx_path;", nullptr, nullptr, nullptr);
        error_string_.clear();
        return true;
    }

    const char *schema_sql =
        R"(
        PRAGMA foreign_keys = ON;

        CREATE TABLE files (
            file_id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            rel_path TEXT,
            file_version INTEGER DEFAULT 1
        );

        CREATE TABLE tags (
            tag TEXT NOT NULL,
            file_id INTEGER NOT NULL,
            PRIMARY KEY (tag, file_id),
            FOREIGN KEY (file_id)
                REFERENCES files(file_id)
                ON DELETE CASCADE
        );

        CREATE INDEX idx_tag ON tags(tag);
        CREATE INDEX idx_tag_file ON tags(file_id);

        PRAGMA user_version = 4;
    )";

    if (version == 0)
    {
        if (sqlite3_exec(db_, schema_sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to create database schema: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        error_string_.clear();
        return true;
    }

    // 数据库只是磁盘缓存 不要求异常情况下保证事务持久性
    // 历史结构不再迁移 直接删除旧表并重新创建
    const char *rebuild_sql =
        R"(
        DROP TABLE IF EXISTS tags;
        DROP TABLE IF EXISTS files;

        CREATE TABLE files (
            file_id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            rel_path TEXT,
            file_version INTEGER DEFAULT 1
        );

        CREATE TABLE tags (
            tag TEXT NOT NULL,
            file_id INTEGER NOT NULL,
            PRIMARY KEY (tag, file_id),
            FOREIGN KEY (file_id)
                REFERENCES files(file_id)
                ON DELETE CASCADE
        );

        CREATE INDEX idx_tag ON tags(tag);
        CREATE INDEX idx_tag_file ON tags(file_id);

        PRAGMA user_version = 4;
    )";

    if (sqlite3_exec(db_, rebuild_sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = "Failed to rebuild database schema: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    error_string_.clear();
    return true;
}

bool FileDatabase::clearAll()
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    if (sqlite3_exec(db_, "DELETE FROM tags;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    if (sqlite3_exec(db_, "DELETE FROM files;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    error_string_.clear();
    return true;
}

bool FileDatabase::insertDirectory(const std::filesystem::path &path_utf8, std::function<std::vector<std::string>(const std::filesystem::path &)> tag_extractor)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    if (!tag_extractor)
    {
        error_string_ = "[warning] Tag extractor is empty";
        return false;
    }

    std::error_code ec;

    if (!std::filesystem::is_directory(path_utf8, ec) || ec)
    {
        error_string_ = "[warning] Directory does not exist or not a directory";
        return false;
    }

    sqlite3_stmt *insert_file_stmt = nullptr;
    sqlite3_stmt *insert_tag_stmt = nullptr;
    const char *insert_file_sql = "INSERT OR REPLACE INTO files (path, rel_path, file_version) VALUES (?, ?, 1);";
    const char *insert_tag_sql = "INSERT INTO tags (tag, file_id) VALUES (?, ?);";
    int result = sqlite3_prepare_v2(db_, insert_file_sql, -1, &insert_file_stmt, nullptr);

    if (result != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    result = sqlite3_prepare_v2(db_, insert_tag_sql, -1, &insert_tag_stmt, nullptr);

    if (result != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(insert_file_stmt);
        return false;
    }

    auto rollback_and_finalize = [&]()
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(insert_file_stmt);
        sqlite3_finalize(insert_tag_stmt);
        insert_file_stmt = nullptr;
        insert_tag_stmt = nullptr;
    };

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(insert_file_stmt);
        sqlite3_finalize(insert_tag_stmt);
        return false;
    }

    const std::string root_path = path_utf8.generic_u8string();
    std::string prefix = root_path;

    if (!prefix.empty() && prefix.back() != '/')
    {
        prefix.push_back('/');
    }

    std::string path_string;
    std::string relative_path;
    int skipped_count = 0;
    std::string skipped_files;
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iter(path_utf8, std::filesystem::directory_options::skip_permission_denied, iterator_error);
    const std::filesystem::recursive_directory_iterator end_iter;

    if (iterator_error)
    {
        error_string_ = "[warning] Failed to open directory: " + iterator_error.message();

        rollback_and_finalize();
        return false;
    }

    for (; iter != end_iter; iter.increment(iterator_error))
    {
        if (iterator_error)
        {
            skipped_count++;

            if (skipped_files.size() < 256)
            {
                if (!skipped_files.empty())
                {
                    skipped_files += ' ';
                }

                skipped_files += "[error: " + iterator_error.message() + "]";
            }

            iterator_error.clear();
            continue;
        }

        const std::filesystem::directory_entry &entry = *iter;
        const std::filesystem::path &entry_path = entry.path();
        std::error_code type_error;
        const bool entry_is_directory = entry.is_directory(type_error);

        if (type_error)
        {
            skipped_count++;
            continue;
        }

        // .tag 目录是标签侧车目录 不作为普通目录扫描
        if (entry_is_directory && entry_path.filename() == ".tag")
        {
            iter.disable_recursion_pending();
            continue;
        }

        path_string = entry_path.generic_u8string();
        relative_path.clear();

        if (path_string.size() > prefix.size() && path_string.compare(0, prefix.size(), prefix) == 0)
        {
            relative_path.assign(path_string, prefix.size(), std::string::npos);
        }
        else
        {
            relative_path = entry_path.lexically_relative(path_utf8).generic_u8string();
        }

        const std::vector<std::string> tags = tag_extractor(entry_path);
        sqlite3_reset(insert_file_stmt);
        result = sqlite3_bind_text(insert_file_stmt, 1, path_string.data(), static_cast<int>(path_string.size()), SQLITE_TRANSIENT);

        if (result != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            rollback_and_finalize();
            return false;
        }

        result = sqlite3_bind_text(insert_file_stmt, 2, relative_path.data(), static_cast<int>(relative_path.size()), SQLITE_TRANSIENT);

        if (result != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            rollback_and_finalize();
            return false;
        }

        result = sqlite3_step(insert_file_stmt);

        if (result != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            rollback_and_finalize();
            return false;
        }

        const sqlite3_int64 file_id = sqlite3_last_insert_rowid(db_);

        for (const std::string &tag : tags)
        {
            if (tag.empty())
            {
                continue;
            }

            sqlite3_reset(insert_tag_stmt);
            result = sqlite3_bind_text(insert_tag_stmt, 1, tag.data(), static_cast<int>(tag.size()), SQLITE_TRANSIENT);

            if (result != SQLITE_OK)
            {
                error_string_ = sqlite3_errmsg(db_);
                rollback_and_finalize();
                return false;
            }

            result = sqlite3_bind_int64(insert_tag_stmt, 2, file_id);

            if (result != SQLITE_OK)
            {
                error_string_ = sqlite3_errmsg(db_);
                rollback_and_finalize();
                return false;
            }

            result = sqlite3_step(insert_tag_stmt);

            if (result != SQLITE_DONE)
            {
                error_string_ = sqlite3_errmsg(db_);
                rollback_and_finalize();
                return false;
            }
        }
    }

    result = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    if (result != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(insert_file_stmt);
        sqlite3_finalize(insert_tag_stmt);

        return false;
    }

    sqlite3_finalize(insert_file_stmt);
    sqlite3_finalize(insert_tag_stmt);

    if (skipped_count > 0)
    {
        error_string_ = "[tip] inserted directory, but " + std::to_string(skipped_count) + " entry(ies) were skipped: " + skipped_files;
    }
    else
    {
        error_string_.clear();
    }

    return true;
}

bool FileDatabase::updateFile(const table::FileInfo &info)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    if (info.path_.empty())
    {
        error_string_ = "[warning] File path is empty";
        return false;
    }

    sqlite3_stmt *stmt = nullptr;

    const char *select_sql = "SELECT file_id, file_version FROM files WHERE path = ?;";

    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, info.path_.data(), static_cast<int>(info.path_.size()), SQLITE_TRANSIENT);

    int existing_id = 0;
    int existing_version = 0;
    const int select_result = sqlite3_step(stmt);

    if (select_result == SQLITE_ROW)
    {
        existing_id = sqlite3_column_int(stmt, 0);
        existing_version = sqlite3_column_int(stmt, 1);
    }
    else if (select_result != SQLITE_DONE)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    stmt = nullptr;
    int file_id = 0;

    if (existing_id == 0)
    {
        const char *insert_sql = "INSERT INTO files (path, rel_path, file_version) VALUES (?, ?, ?);";

        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.path_.data(), static_cast<int>(info.path_.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.rel_path_.data(), static_cast<int>(info.rel_path_.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, info.file_version_);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }

        file_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
    else
    {
        const char *update_sql =
            "UPDATE files "
            "SET rel_path = ?, "
            "file_version = file_version + 1 "
            "WHERE file_id = ? "
            "AND file_version = ?;";

        if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.rel_path_.data(), static_cast<int>(info.rel_path_.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, existing_id);
        sqlite3_bind_int(stmt, 3, existing_version);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }

        if (sqlite3_changes(db_) == 0)
        {
            error_string_ = "[warning] Optimistic lock conflict: file version changed";
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_finalize(stmt);
        stmt = nullptr;
        file_id = existing_id;
    }

    // updateFile 是一个完整的文件更新操作
    // 删除旧标签后重新写入当前标签集合
    const char *delete_tag_sql = "DELETE FROM tags WHERE file_id = ?;";

    if (sqlite3_prepare_v2(db_, delete_tag_sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_int(stmt, 1, file_id);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *insert_tag_sql = "INSERT INTO tags (tag, file_id) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db_, insert_tag_sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    for (const std::string &tag : info.tags_)
    {
        if (tag.empty())
        {
            continue;
        }

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, tag.data(), static_cast<int>(tag.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, file_id);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }
    }

    sqlite3_finalize(stmt);
    error_string_.clear();
    return true;
}

bool FileDatabase::removeFile(const std::filesystem::path &path_utf8)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    const std::string path_string = path_utf8.generic_u8string();
    const char *sql = "DELETE FROM files WHERE path = ?;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, path_string.data(), static_cast<int>(path_string.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);

    error_string_.clear();
    return true;
}

bool FileDatabase::removeDirectory(const std::filesystem::path &dir_path_utf8)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    std::string dir_string = dir_path_utf8.generic_u8string();

    while (dir_string.size() > 1 && dir_string.back() == '/')
    {
        dir_string.pop_back();
    }

    // 防止误删除盘符根目录等危险路径
    if (dir_string.size() < 2 || dir_string.back() == ':')
    {
        error_string_ = "[warning] Refuse to remove database entries of unsafe path: " + dir_string;
        return false;
    }

    std::string prefix = dir_string;

    if (prefix.empty() || prefix.back() != '/')
    {
        prefix.push_back('/');
    }

    const char *sql =
        "DELETE FROM files "
        "WHERE lower(substr(path, 1, length(?))) = lower(?) "
        "OR lower(path) = lower(?);";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, prefix.data(), static_cast<int>(prefix.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, prefix.data(), static_cast<int>(prefix.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, dir_string.data(), static_cast<int>(dir_string.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    const int removed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    error_string_ = "[tip] removed " + std::to_string(removed) + " database entry(ies) of " + dir_string;
    return true;
}

bool FileDatabase::clearRepeat()
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    const char *dedup_sql =
        "DELETE FROM files "
        "WHERE file_id NOT IN ("
        "    SELECT MIN(file_id) "
        "    FROM files "
        "    GROUP BY REPLACE(path, '\\', '/')"
        ");";

    if (sqlite3_exec(db_, dedup_sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    error_string_.clear();
    return true;
}

bool FileDatabase::cleanupInvalid()
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    const char *select_sql = "SELECT file_id, path FROM files;";

    sqlite3_stmt *select_stmt = nullptr;

    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    std::vector<int> invalid_ids;

    while (true)
    {
        const int result = sqlite3_step(select_stmt);

        if (result == SQLITE_DONE)
        {
            break;
        }

        if (result != SQLITE_ROW)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(select_stmt);
            return false;
        }

        const char *path_text = reinterpret_cast<const char *>(sqlite3_column_text(select_stmt, 1));

        if (path_text == nullptr)
        {
            invalid_ids.push_back(sqlite3_column_int(select_stmt, 0));
            continue;
        }

        const std::filesystem::path path(path_text);
        std::error_code ec;

        if (!std::filesystem::exists(path, ec) || ec)
        {
            invalid_ids.push_back(sqlite3_column_int(select_stmt, 0));
        }
    }

    sqlite3_finalize(select_stmt);
    select_stmt = nullptr;

    if (invalid_ids.empty())
    {
        error_string_.clear();
        return true;
    }

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    const char *delete_sql = "DELETE FROM files WHERE file_id = ?;";
    sqlite3_stmt *delete_stmt = nullptr;

    if (sqlite3_prepare_v2(db_, delete_sql, -1, &delete_stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    for (const int file_id : invalid_ids)
    {
        sqlite3_reset(delete_stmt);
        sqlite3_bind_int(delete_stmt, 1, file_id);

        if (sqlite3_step(delete_stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(delete_stmt);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }

    sqlite3_finalize(delete_stmt);

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    error_string_.clear();
    return true;
}

std::vector<table::FileInfo> FileDatabase::searchByTags(const SearchOptions &opts) const
{
    std::vector<table::FileInfo> results;

    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return results;
    }

    std::string sql =
        R"(
        SELECT
            f.file_id,
            f.path,
            f.rel_path,
            f.file_version,
            GROUP_CONCAT(t.tag, ',') AS tags_csv
        FROM files f
        LEFT JOIN tags t
            ON f.file_id = t.file_id
        WHERE 1 = 1
    )";

    std::vector<std::string> bind_values;
    std::set<std::string> only_set;

    for (const std::string &tag : opts.only_)
    {
        if (!tag.empty())
        {
            only_set.insert(tag);
        }
    }

    if (!only_set.empty())
    {
        for (const std::string &tag : only_set)
        {
            sql +=
                " AND EXISTS ("
                "SELECT 1 FROM tags t2 "
                "WHERE t2.file_id = f.file_id "
                "AND t2.tag = ?"
                ")";

            bind_values.push_back(tag);
        }

        sql +=
            " AND NOT EXISTS ("
            "SELECT 1 FROM tags t2 "
            "WHERE t2.file_id = f.file_id "
            "AND t2.tag NOT IN (";

        size_t index = 0;

        for (const std::string &tag : only_set)
        {
            if (index > 0)
            {
                sql += ", ";
            }

            sql += "?";
            index++;
        }

        sql += "))";

        for (const std::string &tag : only_set)
        {
            bind_values.push_back(tag);
        }
    }

    for (const std::string &tag : opts.exclude_)
    {
        if (tag.empty())
        {
            continue;
        }

        sql +=
            " AND NOT EXISTS ("
            "SELECT 1 FROM tags t3 "
            "WHERE t3.file_id = f.file_id "
            "AND t3.tag = ?"
            ")";

        bind_values.push_back(tag);
    }

    std::vector<std::string> valid_include;

    for (const std::string &tag : opts.include_)
    {
        if (!tag.empty())
        {
            valid_include.push_back(tag);
        }
    }

    if (!valid_include.empty())
    {
        sql += " AND (";

        for (size_t index = 0; index < valid_include.size(); index++)
        {
            if (index > 0)
            {
                sql += " OR ";
            }

            sql +=
                "EXISTS ("
                "SELECT 1 FROM tags t4 "
                "WHERE t4.file_id = f.file_id "
                "AND t4.tag = ?"
                ")";

            bind_values.push_back(valid_include[index]);
        }

        sql += ")";
    }

    sql += " GROUP BY f.file_id;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return results;
    }

    int bind_index = 1;

    for (const std::string &value : bind_values)
    {
        sqlite3_bind_text(stmt, bind_index++, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    while (true)
    {
        const int result = sqlite3_step(stmt);

        if (result == SQLITE_DONE)
        {
            break;
        }

        if (result != SQLITE_ROW)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return results;
        }

        table::FileInfo info;
        info.file_id_ = sqlite3_column_int(stmt, 0);
        const char *path_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));

        if (path_text != nullptr)
        {
            info.path_ = path_text;
        }

        const char *relative_path_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        if (relative_path_text != nullptr)
        {
            info.rel_path_ = relative_path_text;
        }

        info.file_version_ = sqlite3_column_int(stmt, 3);
        const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

        if (tags_csv != nullptr)
        {
            const std::string csv(tags_csv);
            size_t begin = 0;

            while (begin < csv.size())
            {
                const size_t end = csv.find(',', begin);

                const size_t length = end == std::string::npos ? csv.size() - begin : end - begin;

                if (length > 0)
                {
                    info.tags_.emplace_back(csv, begin, length);
                }

                if (end == std::string::npos)
                {
                    break;
                }

                begin = end + 1;
            }
        }

        results.push_back(std::move(info));
    }

    sqlite3_finalize(stmt);

    error_string_.clear();
    return results;
}

std::optional<table::FileInfo> FileDatabase::getFileInfo(const std::filesystem::path &path) const
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return std::nullopt;
    }

    const std::string path_string = path.generic_u8string();
    const char *sql =
        R"(
        SELECT
            f.file_id,
            f.path,
            f.rel_path,
            f.file_version,
            GROUP_CONCAT(t.tag, ',') AS tags_csv
        FROM files f
        LEFT JOIN tags t
            ON f.file_id = t.file_id
        WHERE f.path = ?
        GROUP BY f.file_id;
    )";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, path_string.data(), static_cast<int>(path_string.size()), SQLITE_TRANSIENT);
    const int result = sqlite3_step(stmt);

    if (result == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        error_string_.clear();
        return std::nullopt;
    }

    if (result != SQLITE_ROW)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    table::FileInfo info;
    info.file_id_ = sqlite3_column_int(stmt, 0);
    const char *path_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));

    if (path_text != nullptr)
    {
        info.path_ = path_text;
    }

    const char *relative_path_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

    if (relative_path_text != nullptr)
    {
        info.rel_path_ = relative_path_text;
    }

    info.file_version_ = sqlite3_column_int(stmt, 3);
    const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

    if (tags_csv != nullptr)
    {
        const std::string csv(tags_csv);
        size_t begin = 0;

        while (begin < csv.size())
        {
            const size_t end = csv.find(',', begin);
            const size_t length = end == std::string::npos ? csv.size() - begin : end - begin;

            if (length > 0)
            {
                info.tags_.emplace_back(csv, begin, length);
            }

            if (end == std::string::npos)
            {
                break;
            }

            begin = end + 1;
        }
    }

    sqlite3_finalize(stmt);
    error_string_.clear();
    return info;
}

const std::string &FileDatabase::getLastError() const
{
    return error_string_;
}
