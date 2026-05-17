#include "http_server.h"

#include <csignal>
#include <cstdlib>

namespace {

HttpServer* g_server = nullptr;

std::string env_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

unsigned int env_port_or_default(const char* name, unsigned int fallback) {
    const char* value = std::getenv(name);
    return value ? static_cast<unsigned int>(std::strtoul(value, nullptr, 10)) : fallback;
}

int env_int_or_default(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

void handle_signal(int) {
    if (g_server) {
        g_server->requestStop();
    }
}

} // namespace

int main() {
    HttpServerConfig config;
    config.database.host = env_or_default("MINI_DB_HOST", config.database.host);
    config.database.user = env_or_default("MINI_DB_USER", config.database.user);
    config.database.password = env_or_default("MINI_DB_PASSWORD", config.database.password);
    config.database.database = env_or_default("MINI_DB_NAME", config.database.database);
    config.database.port = env_port_or_default("MINI_DB_PORT", config.database.port);
    config.idleTimeoutSeconds =
        env_int_or_default("MINI_IDLE_TIMEOUT_SECONDS", config.idleTimeoutSeconds);
    HttpServer server(config);
    g_server = &server;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    return server.start() ? 0 : 1;
}
