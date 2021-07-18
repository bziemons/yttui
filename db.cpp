// SPDX-License-Identifier: MIT
#include "db.h"

sqlite3 *db = nullptr;

db_transaction::db_transaction()
{
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
}

db_transaction::~db_transaction()
{
    sqlite3_exec(db, "COMMIT TRANSACTION;", nullptr, nullptr, nullptr);
}

std::string get_string(sqlite3_stmt *row, int col)
{
    return std::string((char*)sqlite3_column_text(row, col));
}

int get_int(sqlite3_stmt *row, int col)
{
    return sqlite3_column_int(row, col);
}

void db_check_schema();

void db_init(const std::string &filename)
{
    SC(sqlite3_open(filename.c_str(), &db));
    db_check_schema();
}

void db_shutdown()
{
    sqlite3_close(db);
    db = nullptr;
}

void db_check_schema() {
    bool settings_table_found = false;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table';", -1, &query, nullptr));
    while(sqlite3_step(query) == SQLITE_ROW) {
        if(get_string(query, 0) == "settings")
            settings_table_found = true;
    }
    SC(sqlite3_finalize(query));

    int schema_version = 0;
    if(settings_table_found) {
        const std::string schema_version_str = db_get_setting("schema_version");
        if(!schema_version_str.empty()) {
            schema_version = std::stoi(schema_version_str);
        }
    }
    if(schema_version < 1) {
        const std::string db_init_sql = R"(
PRAGMA foreign_keys;
PRAGMA synchronous = NORMAL;
CREATE TABLE channels (
    channelId TEXT PRIMARY KEY,
    name TEXT
);
CREATE TABLE "settings" (
    key	TEXT NOT NULL PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE videos (
    videoId TEXT PRIMARY KEY,
    channelId TEXT REFERENCES channels(channelId) ON DELETE CASCADE ON UPDATE CASCADE,
    title TEXT,
    description TEXT,
    flags INTEGER DEFAULT 0 NOT NULL,
    published TEXT
);
INSERT INTO settings(key, value) VALUES("schema_version", "1"); )";
        SC(sqlite3_exec(db, db_init_sql.c_str(), nullptr, nullptr, nullptr));
    }
    if(schema_version < 2) {
        const std::string sql = R"(
ALTER TABLE channels ADD COLUMN user_flags INTEGER DEFAULT 0;
CREATE TABLE user_flags (
    flagId INTEGER PRIMARY KEY,
    name TEXT NOT NULL
);
ALTER TABLE videos ADD COLUMN added_to_playlist TEXT;
UPDATE videos SET added_to_playlist = published, published = "";
UPDATE settings SET value="2" WHERE key="schema_version";
)";
        SC(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr));
    }
}

std::string db_get_setting(const std::string &key)
{
    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key = ?1;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, key.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_step(query));
    std::string value = get_string(query, 0);
    SC(sqlite3_finalize(query));
    return value;
}

void db_set_setting(const std::string &key, const std::string &value)
{
    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "INSERT INTO settings(key, value) values(?1, ?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, key.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 2, value.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_step(query));
    SC(sqlite3_finalize(query));
}

std::map<std::string, std::string> db_get_settings(const std::string &prefix)
{
    std::map<std::string, std::string> result;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT key, value FROM settings WHERE key LIKE ?1;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, (prefix + ":").c_str(), -1, SQLITE_TRANSIENT));
    while(sqlite3_step(query) == SQLITE_ROW) {
        const std::string key = get_string(query, 0);
        const std::string value  = get_string(query, 1);
        size_t i = key.find(':');
        if(i != std::string::npos) {
            result.emplace(key.substr(i), value);
        }
    }
    SC(sqlite3_finalize(query));
    return result;
}
