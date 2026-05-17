#include "user_service.h"

namespace {

std::string escape(MYSQL* db, const std::string& value) {
    std::string escaped(value.size() * 2 + 1, '\0');
    unsigned long size = mysql_real_escape_string(
        db, &escaped[0], value.c_str(), static_cast<unsigned long>(value.size()));
    escaped.resize(size);
    return escaped;
}

} // namespace

UserService::UserService(DatabasePool& pool)
    : pool_(pool) {}

bool UserService::registerUser(const std::string& username, const std::string& password) {
    auto lease = pool_.acquire();
    std::string sql =
        "INSERT INTO users(username, password) VALUES('" +
        escape(lease.get(), username) + "', '" +
        escape(lease.get(), password) + "');";
    return mysql_query(lease.get(), sql.c_str()) == 0;
}

bool UserService::loginUser(const std::string& username, const std::string& password) {
    auto lease = pool_.acquire();
    std::string sql =
        "SELECT 1 FROM users WHERE username = '" +
        escape(lease.get(), username) + "' AND password = '" +
        escape(lease.get(), password) + "' LIMIT 1;";
    if (mysql_query(lease.get(), sql.c_str()) != 0) {
        return false;
    }
    MYSQL_RES* result = mysql_store_result(lease.get());
    if (!result) {
        return false;
    }
    bool ok = mysql_num_rows(result) == 1;
    mysql_free_result(result);
    return ok;
}
