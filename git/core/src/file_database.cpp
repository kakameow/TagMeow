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

    const char *schema_sql =
        R"(
        CREATE TABLE files (
            file_id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            rel_path TEXT,
            file_version INTEGER DEFAULT 1
        );
        CREATE INDEX idx_path ON files(path);
        CREATE TABLE tags (
            tag TEXT NOT NULL,
            file_id INTEGER NOT NULL,
            PRIMARY KEY (tag, file_id),
            FOREIGN KEY (file_id) REFERENCES files(file_id) ON DELETE CASCADE
        );
        CREATE INDEX idx_tag ON tags(tag);
        CREATE INDEX idx_tag_file ON tags(file_id);
        PRAGMA user_version = 4;
    )";

    // 版本 0 首次创建
    if (version == 0)
    {
        if (sqlite3_exec(db_, schema_sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to create tables: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        error_string_.clear();
        return true;
    }

    // 其它历史版本：schema 已精简（去掉 mtime/size/sidecar_mtime/last_refresh_time）
    // 数据库只是磁盘缓存 直接重建表 内容由下一次刷新重新扫描得到
    {
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
            CREATE INDEX idx_path ON files(path);
            CREATE TABLE tags (
                tag TEXT NOT NULL,
                file_id INTEGER NOT NULL,
                PRIMARY KEY (tag, file_id),
                FOREIGN KEY (file_id) REFERENCES files(file_id) ON DELETE CASCADE
            );
            CREATE INDEX idx_tag ON tags(tag);
            CREATE INDEX idx_tag_file ON tags(file_id);
            PRAGMA user_version = 4;
        )";

        if (sqlite3_exec(db_, rebuild_sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            error_string_ = "Failed to rebuild tables: " + std::string(sqlite3_errmsg(db_));
            return false;
        }

        error_string_.clear();
        return true;
    }
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

    // 先删关联表 不依赖外键级联
    if (sqlite3_exec(db_, "DELETE FROM tags;", nullptr, nullptr, nullptr) != SQLITE_OK ||
        sqlite3_exec(db_, "DELETE FROM files;", nullptr, nullptr, nullptr) != SQLITE_OK)
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
    if (!std::filesystem::exists(path_utf8) || !std::filesystem::is_directory(path_utf8))
    {
        error_string_ = "[warning] Directory does not exist or not a directory";
        return false;
    }

    sqlite3_stmt *ins_file = nullptr;
    sqlite3_stmt *ins_tag = nullptr;
    const char *ins_file_sql = "INSERT OR REPLACE INTO files (path, rel_path, file_version) VALUES (?, ?, 1);";
    const char *ins_tag_sql = "INSERT OR REPLACE INTO tags (tag, file_id) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db_, ins_file_sql, -1, &ins_file, nullptr) != SQLITE_OK || sqlite3_prepare_v2(db_, ins_tag_sql, -1, &ins_tag, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        if (ins_file != nullptr)
        {
            sqlite3_finalize(ins_file);
        }
        if (ins_tag != nullptr)
        {
            sqlite3_finalize(ins_tag);
        }
        return false;
    }

    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(ins_file);
        sqlite3_finalize(ins_tag);
        return false;
    }

    std::string prefix = path_utf8.generic_u8string();
    if (!prefix.empty() && prefix.back() != '/')
    {
        prefix += '/';
    }

    int iter_skip_count = 0;
    std::string iter_skip_files;

    std::error_code ec_iter;
    std::filesystem::recursive_directory_iterator iter(path_utf8, ec_iter);
    std::filesystem::recursive_directory_iterator end_iter;

    for (; iter != end_iter; iter++)
    {
        if (ec_iter)
        {
            iter_skip_count++;
            if (iter_skip_files.size() < 256)
            {
                iter_skip_files += "[error: " + ec_iter.message() + "]";
            }
            ec_iter.clear();
            continue;
        }

        const std::filesystem::directory_entry &entry = *iter;
        const std::filesystem::path &entry_path = entry.path();

        std::error_code ec_type;
        const bool entry_is_directory = entry.is_directory(ec_type);
        if (ec_type)
        {
            iter_skip_count++;
            continue;
        }

        if (entry_is_directory && entry_path.filename() == ".tag")
        {
            iter.disable_recursion_pending();
            continue;
        }


        const std::string path_str = entry_path.generic_u8string();
        std::string rel_path;
        if (path_str.size() > prefix.size() && path_str.compare(0, prefix.size(), prefix) == 0)
        {
            rel_path = path_str.substr(prefix.size());
        }
        else
        {
            std::error_code ec_rel;
            rel_path = std::filesystem::relative(entry_path, path_utf8, ec_rel).generic_u8string();
        }


        const std::vector<std::string> tags = tag_extractor(entry_path);

        sqlite3_reset(ins_file);
        sqlite3_clear_bindings(ins_file);
        sqlite3_bind_text(ins_file, 1, path_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_file, 2, rel_path.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(ins_file) != SQLITE_DONE)
        {
            error_string_ = sqlite3_errmsg(db_);
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_finalize(ins_file);
            sqlite3_finalize(ins_tag);
            return false;
        }

        const int file_id = static_cast<int>(sqlite3_last_insert_rowid(db_));

        for (const auto &tag : tags)
        {
            if (tag.empty())
            {
                continue;
            }

            sqlite3_reset(ins_tag);
            sqlite3_clear_bindings(ins_tag);
            sqlite3_bind_text(ins_tag, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ins_tag, 2, file_id);

            if (sqlite3_step(ins_tag) != SQLITE_DONE)
            {
                error_string_ = sqlite3_errmsg(db_);
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                sqlite3_finalize(ins_file);
                sqlite3_finalize(ins_tag);
                return false;
            }
        }
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_string_ = sqlite3_errmsg(db_);
        sqlite3_finalize(ins_file);
        sqlite3_finalize(ins_tag);
        return false;
    }

    sqlite3_finalize(ins_file);
    sqlite3_finalize(ins_tag);

    if (iter_skip_count > 0)
    {
        error_string_ = "[tip] inserted directory, but " + std::to_string(iter_skip_count) + " entry(ies) were skipped: " + iter_skip_files;
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

    int file_id = 0;

    if (existing_id == 0)
    {
        const char *insert_sql =
            R"(
            INSERT INTO files (path, rel_path, file_version)
            VALUES (?, ?, ?);
        )";

        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.path_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, info.rel_path_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, info.file_version_);

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
            UPDATE files SET rel_path=?, file_version=file_version+1
            WHERE file_id=? AND file_version=?;
        )";

        if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            error_string_ = sqlite3_errmsg(db_);
            return false;
        }

        sqlite3_bind_text(stmt, 1, info.rel_path_.c_str(), -1, SQLITE_STATIC);
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
        f.file_id, f.path, f.rel_path, f.file_version,
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
        for (size_t i = 0; i < only_set.size(); i++)
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
            for (size_t i = 0; i < valid_include.size(); i++)
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
        info.file_version_ = sqlite3_column_int(stmt, 3);

        const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
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
        SELECT f.file_id, f.path, f.rel_path, f.file_version,
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
        info.file_version_ = sqlite3_column_int(stmt, 3);

        const char *tags_csv = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
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
