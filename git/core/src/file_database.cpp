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
    std::error_code ec;
    auto parent = db_path_utf8.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
    }

    int rc = sqlite3_open(db_path_utf8.u8string().c_str(), &db_);
    if (rc != SQLITE_OK)
    {
        error_string_ = "[warning] Failed to open database: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (initSchema())
    {
        error_string_.clear();
        return true;
    }

    error_string_ = "[warning] initialization failed";
    return false;
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
    }

    // 版本 0 首次创建
    if (version == 0)
    {
        const char *create_files =
            R"(
            CREATE TABLE files (
                file_id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT UNIQUE NOT NULL,
                rel_path TEXT,
                file_mtime INTEGER,
                file_size INTEGER,
                sidecar_mtime INTEGER,
                file_version INTEGER DEFAULT 1,
                last_refresh_time INTEGER
            );
            CREATE INDEX idx_path ON files(path);
        )";
        const char *create_tags =
            R"(
            CREATE TABLE tags (
                tag TEXT NOT NULL,
                file_id INTEGER NOT NULL,
                PRIMARY KEY (tag, file_id),
                FOREIGN KEY (file_id) REFERENCES files(file_id) ON DELETE CASCADE
            );
            CREATE INDEX idx_tag ON tags(tag);
        )";

        if (sqlite3_exec(db_, create_files, nullptr, nullptr, nullptr) != SQLITE_OK || sqlite3_exec(db_, create_tags, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to create tables: " + std::string(sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_exec(db_, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr);
        error_string_.clear();
        return true;
    }

    // 版本 1 迁移
    if (version == 1)
    {
        const char *create_new =
            R"(
            CREATE TABLE files_new (
                file_id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT UNIQUE NOT NULL,
                rel_path TEXT,
                file_mtime INTEGER,
                file_size INTEGER,
                sidecar_mtime INTEGER,
                file_version INTEGER DEFAULT 1,
                last_refresh_time INTEGER
            );
        )";
        if (sqlite3_exec(db_, create_new, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to create new files table: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        const char *copy_data =
            R"(
            INSERT INTO files_new
            (file_id, path, rel_path, file_mtime, file_size,
                sidecar_mtime, file_version, last_refresh_time)
            SELECT file_id, path, rel_path, file_mtime, file_size,
                sidecar_mtime, file_version, last_refresh_time
            FROM files;
        )";

        if (sqlite3_exec(db_, copy_data, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to copy data: " + std::string(sqlite3_errmsg(db_));
            sqlite3_exec(db_, "DROP TABLE files_new;", nullptr, nullptr, nullptr);
            return false;
        }

        if (sqlite3_exec(db_, "DROP TABLE files;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to drop old files table: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        if (sqlite3_exec(db_, "ALTER TABLE files_new RENAME TO files;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to rename table: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        const char *recreate_idx =
            R"(
            CREATE INDEX IF NOT EXISTS idx_path ON files(path);
        )";
        if (sqlite3_exec(db_, recreate_idx, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to recreate indexes: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        sqlite3_stmt *check = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT name FROM sqlite_master WHERE type='table' AND name='tags';", -1, &check, nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(check) != SQLITE_ROW)
            {
                sqlite3_finalize(check);
                const char *create_tags =
                    R"(
                    CREATE TABLE tags (
                        tag TEXT NOT NULL,
                        file_id INTEGER NOT NULL,
                        PRIMARY KEY (tag, file_id),
                        FOREIGN KEY (file_id) REFERENCES files(file_id) ON DELETE CASCADE
                    );
                    CREATE INDEX idx_tag ON tags(tag);
                )";
                if (sqlite3_exec(db_, create_tags, nullptr, nullptr, nullptr) != SQLITE_OK)
                {
                    error_string_ = "Failed to create tags table during migration: " + std::string(sqlite3_errmsg(db_));
                    return false;
                }
            }
            sqlite3_finalize(check);
        }

        sqlite3_exec(db_, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr);
        error_string_.clear();
        return true;
    }

    // 版本 2 当前 schema 无需迁移
    if (version == 2)
    {
        error_string_.clear();
        return true;
    }

    error_string_ = "[warning] unknown database version: " + std::to_string(version);
    return false;
}

bool FileDatabase::updateDirectory(const std::filesystem::path &path_utf8, std::function<std::vector<std::string>(const std::filesystem::path &)> tag_extractor)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }
    if (!std::filesystem::exists(path_utf8) || !std::filesystem::is_directory(path_utf8))
    {
        error_string_ = "[warning] Directory does not exist or not a directory";
        return false;
    }

    std::set<std::string> current_paths;
    std::error_code ec_iter;

    std::filesystem::recursive_directory_iterator iter(path_utf8, ec_iter);
    std::filesystem::recursive_directory_iterator end_iter;

    int iter_skip_count = 0; 
    std::string iter_skip_files;

    for (; iter != end_iter; iter++)
    {
        if (ec_iter)
        {
            iter_skip_count++;
            if (!iter_skip_files.empty())
            {
                iter_skip_files += ", ";
            }
            if (iter_skip_files.size() < 256) 
            {
                iter_skip_files += "[error: " + ec_iter.message() + "]";
                ec_iter.clear();
            }
            continue;
        }
        if (iter->is_directory() && iter->path().filename() == ".tag")
        {
            iter.disable_recursion_pending();
            continue;
        }
        current_paths.insert(iter->path().generic_u8string());
    }

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    // 先删后插 保持一致性
    std::string prefix = path_utf8.generic_u8string();
    if (!prefix.empty() && prefix.back() != '/')
    {
        prefix += '/';
    }

    const char *del_sql = "DELETE FROM files WHERE substr(path, 1, length(?)) = ?;";
    sqlite3_stmt *del_stmt = nullptr;

    if (sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_text(del_stmt, 1, prefix.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(del_stmt, 2, prefix.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(del_stmt) != SQLITE_DONE)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(del_stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_finalize(del_stmt);
    int skipped_count = 0;
    std::string skipped_files;

    for (const auto &path_str : current_paths)
    {
        std::filesystem::path p(path_str);
        auto tags = tag_extractor(p);

        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(p, ec);
        auto size = std::filesystem::is_directory(p) ? 0 : std::filesystem::file_size(p, ec);
        if (ec)
        {
            skipped_count++;
            if (!skipped_files.empty())
            {
                skipped_files += ", ";
            }
            if (skipped_files.size() < 256)
            {
                skipped_files += p.filename().u8string();
            }
            continue;
        }

        auto file_now = std::filesystem::file_time_type::clock::now();
        auto sys_time = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(ftime - file_now);
        auto file_time = std::chrono::duration_cast<std::chrono::seconds>(sys_time.time_since_epoch()).count();

        table::FileInfo info;
        info.path_ = path_str;
        info.rel_path_ = std::filesystem::relative(p, path_utf8).generic_u8string();
        info.file_mtime_ = file_time;
        info.file_size_ = static_cast<int64_t>(size);
        info.sidecar_mtime_ = 0;
        info.tags_ = tags;
        info.file_version_ = 1;

        if (!updateFile(info))
        {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    if (skipped_count > 0)
    {
        error_string_ = "[tip] get directory, but " + std::to_string(iter_skip_count) + " file(s) were skipped due to get errors: " + iter_skip_files;
        error_string_ += "\n[tip] Updated directory, but " + std::to_string(skipped_count) + " file(s) were skipped due to read errors: " + skipped_files;
    }
    else
    {
        error_string_.clear();
    }

    error_string_.clear();
    return true;
}

bool FileDatabase::updateFile(const table::FileInfo &info)
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *check_sql = "SELECT file_id, file_version FROM files WHERE path = ?;";
    if (sqlite3_prepare_v2(db_, check_sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, info.path_.c_str(), -1, SQLITE_STATIC);
    int existing_id = 0;
    int existing_version = 0;

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        existing_id = sqlite3_column_int(stmt, 0);
        existing_version = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int file_id = 0;

    if (existing_id == 0)
    {
        const char *insert_sql =
            R"(
            INSERT INTO files (path, rel_path, file_mtime, 
            file_size, sidecar_mtime, file_version, last_refresh_time)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )";

        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.path_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, info.rel_path_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, info.file_mtime_);
        sqlite3_bind_int64(stmt, 4, info.file_size_);
        sqlite3_bind_int64(stmt, 5, info.sidecar_mtime_);
        sqlite3_bind_int(stmt, 6, info.file_version_);
        sqlite3_bind_int64(stmt, 7, now);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }
        file_id = sqlite3_last_insert_rowid(db_);
        sqlite3_finalize(stmt);
    }
    else
    {
        const char *update_sql =
            R"(
            UPDATE files SET rel_path=?, file_mtime=?, file_size=?, sidecar_mtime=?, 
            file_version=file_version+1, last_refresh_time=?
            WHERE file_id=? AND file_version=?;
        )";

        if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.rel_path_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, info.file_mtime_);
        sqlite3_bind_int64(stmt, 3, info.file_size_);
        sqlite3_bind_int64(stmt, 4, info.sidecar_mtime_);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_bind_int(stmt, 6, existing_id);
        sqlite3_bind_int(stmt, 7, existing_version);

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
        file_id = existing_id;
    }

    // 维护 tags 表 删除旧关联 插入新关联
    // 先删除该文件的所有旧标签
    const char *del_sql = "DELETE FROM tags WHERE file_id = ?;";
    if (sqlite3_prepare_v2(db_, del_sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    const char *ins_sql = "INSERT INTO tags (tag, file_id) VALUES (?, ?);";
    for (const auto &tag : info.tags_)
    {
        if (tag.empty())
        {
            continue;
        }
        if (sqlite3_prepare_v2(db_, ins_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, file_id);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_finalize(stmt);
    }

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

    std::string path_str = path_utf8.generic_u8string();
    const char *sql = "DELETE FROM files WHERE path = ?;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, path_str.c_str(), -1, SQLITE_STATIC);
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

    std::string prefix = dir_path_utf8.generic_u8string();
    if (!prefix.empty() && prefix.back() != '/')
    {
        prefix += '/';
    }

    const char *sql = "DELETE FROM files WHERE substr(path, 1, length(?)) = ?;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, prefix.c_str(), -1, SQLITE_STATIC);
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

bool FileDatabase::clearRepeat()
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return false;
    }

    const char *dedup_sql =
        "DELETE FROM files WHERE file_id NOT IN ("
        "SELECT MIN(file_id) FROM files GROUP BY REPLACE(path, '\\', '/'));";

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
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return false;
    }

    std::vector<int> invalid_ids;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *path_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (path_text != nullptr)
        {
            std::filesystem::path p(path_text);
            if (!std::filesystem::exists(p))
            {
                invalid_ids.push_back(sqlite3_column_int(stmt, 0));
            }
        }
    }
    sqlite3_finalize(stmt);

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

    const char *del_sql = "DELETE FROM files WHERE file_id = ?;";
    for (int id : invalid_ids)
    {
        if (sqlite3_prepare_v2(db_, del_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
        sqlite3_finalize(stmt);
    }

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
        f.file_id, f.path, f.rel_path, f.file_mtime,
        f.file_size, f.sidecar_mtime, f.file_version, f.last_refresh_time,
        GROUP_CONCAT(t.tag, ',') AS tags_csv
        FROM files f
        LEFT JOIN tags t ON f.file_id = t.file_id
        WHERE 1=1
    )";

    std::vector<std::string> bind_values;
    std::vector<std::string> only_tags = opts.only_;
    std::vector<std::string> exclude_tags = opts.exclude_;
    std::vector<std::string> include_tags = opts.include_;

    // only_ 条件 文件标签集合必须恰好等于 only 标签集合 不允许存在额外标签
    std::set<std::string> only_set;
    for (const auto &tag : only_tags)
    {
        if (!tag.empty())
        {
            only_set.insert(tag);
        }
    }

    if (!only_set.empty())
    {
        for (const auto &tag : only_set)
        {
            sql += " AND EXISTS (SELECT 1 FROM tags t2 WHERE t2.file_id = f.file_id AND t2.tag = ?)";
            bind_values.push_back(tag);
        }

        sql += " AND NOT EXISTS (SELECT 1 FROM tags t2 WHERE t2.file_id = f.file_id AND t2.tag NOT IN (";
        for (size_t i = 0; i < only_set.size(); ++i)
        {
            if (i > 0)
            {
                sql += ", ";
            }
            sql += "?";
        }
        sql += "))";

        for (const auto &tag : only_set)
        {
            bind_values.push_back(tag);
        }
    }

    // exclude_ 条件 不能包含任何 exclude 标签
    for (const auto &tag : exclude_tags)
    {
        if (tag.empty())
        {
            continue;
        }
        sql += " AND NOT EXISTS (SELECT 1 FROM tags t3 WHERE t3.file_id = f.file_id AND t3.tag = ?)";
        bind_values.push_back(tag);
    }

    // include_ 条件 至少包含一个 include 标签
    if (!include_tags.empty())
    {
        std::vector<std::string> valid_include;
        for (const auto &tag : include_tags)
        {
            if (!tag.empty())
            {
                valid_include.push_back(tag);
            }
        }
        if (!valid_include.empty())
        {
            sql += " AND (";
            for (size_t i = 0; i < valid_include.size(); ++i)
            {
                if (i > 0)
                {
                    sql += " OR ";
                }
                sql += "EXISTS (SELECT 1 FROM tags t4 WHERE t4.file_id = f.file_id AND t4.tag = ?)";
                bind_values.push_back(valid_include[i]);
            }
            sql += ")";
        }
    }

    sql += " GROUP BY f.file_id;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return results;
    }

    int idx = 1;
    for (const auto &val : bind_values)
    {
        sqlite3_bind_text(stmt, idx++, val.c_str(), -1, SQLITE_STATIC);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        table::FileInfo info;
        info.file_id_ = sqlite3_column_int(stmt, 0);
        info.path_ = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        info.rel_path_ = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        info.file_mtime_ = sqlite3_column_int64(stmt, 3);
        info.file_size_ = sqlite3_column_int64(stmt, 4);
        info.sidecar_mtime_ = sqlite3_column_int64(stmt, 5);
        info.file_version_ = sqlite3_column_int(stmt, 6);
        info.last_refresh_time_ = sqlite3_column_int64(stmt, 7);

        const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
        if (tags_csv)
        {
            std::string csv(tags_csv);
            size_t pos = 0;
            while ((pos = csv.find(',')) != std::string::npos)
            {
                std::string tag = csv.substr(0, pos);
                if (!tag.empty())
                {
                    info.tags_.push_back(tag);
                }
                csv.erase(0, pos + 1);
            }
            if (!csv.empty())
            {
                info.tags_.push_back(csv);
            }
        }

        results.push_back(info);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::optional<table::FileInfo> FileDatabase::getFileInfo(const std::filesystem::path &path) const
{
    if (!db_)
    {
        error_string_ = "[warning] Database not opened";
        return std::nullopt;
    }

    std::string path_str = path.generic_u8string();

    const char *sql =
        R"(
        SELECT f.file_id, f.path, f.rel_path, f.file_mtime,
        f.file_size, f.sidecar_mtime, f.file_version, f.last_refresh_time,
        GROUP_CONCAT(t.tag, ',') AS tags_csv
        FROM files f
        LEFT JOIN tags t ON f.file_id = t.file_id
        WHERE f.path = ?
        GROUP BY f.file_id;
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, path_str.c_str(), -1, SQLITE_STATIC);
    table::FileInfo info;
    bool found = false;

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        found = true;
        info.file_id_ = sqlite3_column_int(stmt, 0);
        info.path_ = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        info.rel_path_ = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        info.file_mtime_ = sqlite3_column_int64(stmt, 3);
        info.file_size_ = sqlite3_column_int64(stmt, 4);
        info.sidecar_mtime_ = sqlite3_column_int64(stmt, 5);
        info.file_version_ = sqlite3_column_int(stmt, 6);
        info.last_refresh_time_ = sqlite3_column_int64(stmt, 7);

        const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
        if (tags_csv)
        {
            std::string csv(tags_csv);
            size_t pos = 0;
            while ((pos = csv.find(',')) != std::string::npos)
            {
                std::string tag = csv.substr(0, pos);
                if (!tag.empty())
                {
                    info.tags_.push_back(tag);
                }
                csv.erase(0, pos + 1);
            }
            if (!csv.empty())
            {
                info.tags_.push_back(csv);
            }
        }
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<table::FileInfo>(info) : std::nullopt;
}

const std::string &FileDatabase::getLastError() const
{
    return error_string_;
}