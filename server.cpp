#include "http_server.h"

int main() {
    HttpServerConfig config;
    HttpServer server(config);
    return server.start() ? 0 : 1;
}
