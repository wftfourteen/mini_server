#ifndef DATABASE_POOL_H
#define DATABASE_POOL_H

#include "mysql_compat.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

struct DatabaseConfig {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    unsigned int port;
    std::size_t poolSize;

    DatabaseConfig()
        : host("127.0.0.1")
        , user("root")
        , password("")
        , database("mini_server")
        , port(3306)
        , poolSize(4) {}
};

class DatabasePool {
public:
    class Lease {
    public:
        Lease();
        Lease(DatabasePool* pool, MYSQL* db);
        ~Lease();

        Lease(Lease&& other);
        Lease& operator=(Lease&& other);

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        MYSQL* get() const { return db_; }
        explicit operator bool() const { return db_ != nullptr; }

    private:
        DatabasePool* pool_;
        MYSQL* db_;
    };

    explicit DatabasePool(const DatabaseConfig& config);
    ~DatabasePool();

    bool init();
    Lease acquire();

private:
    void release(MYSQL* db);

    DatabaseConfig config_;
    std::vector<MYSQL*> all_;
    std::queue<MYSQL*> available_;
    std::mutex mutex_;
    std::condition_variable cv_;

    friend class Lease;
};

#endif // DATABASE_POOL_H
