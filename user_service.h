#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include "database_pool.h"

#include <string>

class UserService {
public:
    explicit UserService(DatabasePool& pool);

    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password);

private:
    DatabasePool& pool_;
};

#endif // USER_SERVICE_H
