#include "database_pool.h"

#include <iostream>

DatabasePool::Lease::Lease()
    : pool_(nullptr)
    , db_(nullptr) {}

DatabasePool::Lease::Lease(DatabasePool* pool, MYSQL* db)
    : pool_(pool)
    , db_(db) {}

DatabasePool::Lease::~Lease() {
    if (pool_ && db_) {
        pool_->release(db_);
    }
}

DatabasePool::Lease::Lease(Lease&& other)
    : pool_(other.pool_)
    , db_(other.db_) {
    other.pool_ = nullptr;
    other.db_ = nullptr;
}

DatabasePool::Lease& DatabasePool::Lease::operator=(Lease&& other) {
    if (this != &other) {
        if (pool_ && db_) {
            pool_->release(db_);
        }
        pool_ = other.pool_;
        db_ = other.db_;
        other.pool_ = nullptr;
        other.db_ = nullptr;
    }
    return *this;
}

DatabasePool::DatabasePool(const DatabaseConfig& config)
    : config_(config) {}

DatabasePool::~DatabasePool() {
    for (auto* db : all_) {
        mysql_close(db);
    }
    mysql_server_end();
}

bool DatabasePool::init() {
    for (std::size_t i = 0; i < config_.poolSize; ++i) {
        MYSQL* db = mysql_init(nullptr);
        if (!db) {
            std::cerr << "[error] mysql_init failed" << std::endl;
            return false;
        }
        if (!mysql_real_connect(db,
                                config_.host.c_str(),
                                config_.user.c_str(),
                                config_.password.c_str(),
                                config_.database.c_str(),
                                config_.port,
                                nullptr,
                                0)) {
            std::cerr << "[error] mysql connect failed: " << mysql_error(db) << std::endl;
            mysql_close(db);
            return false;
        }
        all_.push_back(db);
        available_.push(db);
    }

    Lease lease = acquire();
    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT PRIMARY KEY AUTO_INCREMENT,"
        "username VARCHAR(128) NOT NULL UNIQUE,"
        "password VARCHAR(255) NOT NULL"
        ");";
    if (mysql_query(lease.get(), sql) != 0) {
        std::cerr << "[error] mysql schema init failed: " << mysql_error(lease.get()) << std::endl;
        return false;
    }
    return true;
}

DatabasePool::Lease DatabasePool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
        return !available_.empty();
    });
    MYSQL* db = available_.front();
    available_.pop();
    return Lease(this, db);
}

void DatabasePool::release(MYSQL* db) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(db);
    }
    cv_.notify_one();
}
