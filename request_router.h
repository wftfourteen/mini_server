#ifndef REQUEST_ROUTER_H
#define REQUEST_ROUTER_H

#include "http_request.h"
#include "user_service.h"

#include <string>

struct RoutedResponse {
    int statusCode;
    std::string body;
    std::string contentType;
};

class RequestRouter {
public:
    explicit RequestRouter(UserService& users);

    bool handles(const HttpRequest& request) const;
    RoutedResponse route(const HttpRequest& request) const;

private:
    static std::string formValue(const std::string& body, const std::string& key);
    UserService& users_;
};

#endif // REQUEST_ROUTER_H
