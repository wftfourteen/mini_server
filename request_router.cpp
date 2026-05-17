#include "request_router.h"

RequestRouter::RequestRouter(UserService& users)
    : users_(users) {}

bool RequestRouter::handles(const HttpRequest& request) const {
    return request.getPath() == "/register" || request.getPath() == "/login";
}

RoutedResponse RequestRouter::route(const HttpRequest& request) const {
    if (request.getMethod() != HttpMethod::POST) {
        return {405, "{\"error\":\"method not allowed\"}", "application/json; charset=utf-8"};
    }

    std::string username = formValue(request.getBody(), "username");
    std::string password = formValue(request.getBody(), "password");
    if (username.empty() || password.empty()) {
        return {400, "{\"error\":\"username and password required\"}", "application/json; charset=utf-8"};
    }

    if (request.getPath() == "/register") {
        if (users_.registerUser(username, password)) {
            return {201, "{\"message\":\"registered\"}", "application/json; charset=utf-8"};
        }
        return {409, "{\"error\":\"username already exists\"}", "application/json; charset=utf-8"};
    }

    if (users_.loginUser(username, password)) {
        return {200, "{\"message\":\"login ok\"}", "application/json; charset=utf-8"};
    }
    return {401, "{\"error\":\"invalid credentials\"}", "application/json; charset=utf-8"};
}

std::string RequestRouter::formValue(const std::string& body, const std::string& key) {
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t end = body.find('&', start);
        std::string pair = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) {
            return HttpRequest::urlDecode(pair.substr(eq + 1));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}
