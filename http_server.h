#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "connection.h"
#include "database_pool.h"
#include "epoller.h"
#include "logger.h"
#include "request_router.h"
#include "thread_pool.h"
#include "timer_heap.h"
#include "user_service.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct HttpServerConfig {
    int port;
    int maxEvents;
    int readBufferSize;
    int workerThreads;
    int idleTimeoutSeconds;
    std::string htmlRoot;
    std::string accessLogPath;
    DatabaseConfig database;

    HttpServerConfig()
        : port(8080)
        , maxEvents(1024)
        , readBufferSize(8192)
        , workerThreads(4)
        , idleTimeoutSeconds(30)
        , htmlRoot("webroot")
        , accessLogPath("logs/access.log") {}
};

class HttpServer {
public:
    explicit HttpServer(const HttpServerConfig& config);
    ~HttpServer();

    bool start();
    void stop();
    void requestStop() { running_ = false; }

private:
    bool initListenSocket();
    void eventLoop();
    void handleAccept();
    void dispatchRead(int fd);
    void dispatchWrite(int fd);
    void handleRead(int fd);
    void handleWrite(int fd);
    void processConnectionTask(int fd);
    void closeConnection(int fd);
    void closeIdleConnections();
    std::shared_ptr<Connection> getConnection(int fd);
    bool beginProcessing(const std::shared_ptr<Connection>& conn);
    void finishProcessing(const std::shared_ptr<Connection>& conn);

    HttpServerConfig config_;
    int listenFd_;
    bool running_;
    Epoller epoller_;
    ThreadPool workers_;
    Logger& logger_;
    DatabasePool databasePool_;
    UserService userService_;
    RequestRouter router_;
    TimerHeap timers_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connectionsMutex_;
};

#endif // HTTP_SERVER_H
