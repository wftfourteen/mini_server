#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "connection.h"
#include "epoller.h"
#include "thread_pool.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct HttpServerConfig {
    int port;
    int maxEvents;
    int readBufferSize;
    int workerThreads;
    std::string htmlRoot;

    HttpServerConfig()
        : port(8080)
        , maxEvents(1024)
        , readBufferSize(8192)
        , workerThreads(4)
        , htmlRoot("webroot") {}
};

class HttpServer {
public:
    explicit HttpServer(const HttpServerConfig& config);
    ~HttpServer();

    bool start();
    void stop();

private:
    bool initListenSocket();
    void eventLoop();
    void handleAccept();
    void handleRead(int fd);
    void handleWrite(int fd);
    void processConnectionTask(int fd);
    void closeConnection(int fd);
    std::shared_ptr<Connection> getConnection(int fd);
    void scheduleIfIdle(const std::shared_ptr<Connection>& conn);

    HttpServerConfig config_;
    int listenFd_;
    bool running_;
    Epoller epoller_;
    ThreadPool workers_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connectionsMutex_;
};

#endif // HTTP_SERVER_H
