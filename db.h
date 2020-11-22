#pragma once

#include <map>
#include <sqlite3.h>
#include <string>

extern sqlite3 *db;

extern void tui_abort(const char *fmt, ...);
#define SC(x) { const int res = (x); if(res != SQLITE_OK && res != SQLITE_ROW && res != SQLITE_DONE) { tui_abort("Database error:\n%s failed: (%d) %s", #x, res, sqlite3_errstr(res)); }}

class db_transaction {
public:
    db_transaction();
    ~db_transaction();
};
std::string get_string(sqlite3_stmt *row, int col);

void db_check_schema();

std::string db_get_setting(const std::string &key);
void db_set_setting(const std::string &key, const std::string &value);
std::map<std::string, std::string> db_get_settings(const std::string &prefix);

