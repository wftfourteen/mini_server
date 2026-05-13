#include "http_server.h"

#include "http_request.h"
#include "http_response.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

HttpServer::HttpServer(const HttpServerConfig& config)
    : config_(config)
    , listenFd_(-1)
    , running_(false)
    , workers_(static_cast<std::size_t>(config.workerThreads)) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (!epoller_.valid() || !initListenSocket()) {
        return false;
    }

    if (!epoller_.add(listenFd_, EPOLLIN | EPOLLET)) {
        return false;
    }

    running_ = true;
    workers_.start();

    std::cout << "========================================" << std::endl;
    std::cout << "  MiniServer v3 - thread pool + keep-alive" << std::endl;
    std::cout << "  port: " << config_.port << std::endl;
    std::cout << "  static root: " << config_.htmlRoot << std::endl;
    std::cout << "  worker threads: " << config_.workerThreads << std::endl;
    std::cout << "========================================" << std::endl;

    eventLoop();
    return true;
}

void HttpServer::stop() {
    if (!running_ && listenFd_ == -1) {
        return;
    }

    running_ = false;
    workers_.stop();

    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (auto& pair : connections_) {
            pair.second->markClosed();
            close(pair.first);
        }
        connections_.clear();
    }

    if (listenFd_ != -1) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

bool HttpServer::initListenSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ == -1) {
        std::cerr << "[error] socket failed: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<uint16_t>(config_.port));

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        std::cerr << "[error] bind failed: " << strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (listen(listenFd_, 128) == -1) {
        std::cerr << "[error] listen failed: " << strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (!set_nonblocking(listenFd_)) {
        std::cerr << "[error] set_nonblocking failed: " << strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    return true;
}

void HttpServer::eventLoop() {
    std::vector<epoll_event> events(static_cast<std::size_t>(config_.maxEvents));

    while (running_) {
        int nfds = epoller_.wait(events, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::cerr << "[error] epoll_wait failed: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[static_cast<std::size_t>(i)].data.fd;
            uint32_t ev = events[static_cast<std::size_t>(i)].events;

            if (fd == listenFd_) {
                handleAccept();
            } else if (ev & (EPOLLHUP | EPOLLERR)) {
                closeConnection(fd);
            } else {
                if (ev & EPOLLIN) handleRead(fd);
                if (ev & EPOLLOUT) handleWrite(fd);
            }
        }
    }
}

void HttpServer::handleAccept() {
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int connFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (connFd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cerr << "[error] accept failed: " << strerror(errno) << std::endl;
            return;
        }

        set_nonblocking(connFd);

        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            connections_[connFd] = std::make_shared<Connection>(connFd);
        }

        std::cout << "[accept] " << inet_ntoa(clientAddr.sin_addr)
                  << ":" << ntohs(clientAddr.sin_port)
                  << " fd=" << connFd << std::endl;

        epoller_.add(connFd, EPOLLIN | EPOLLET);
    }
}

void HttpServer::handleRead(int fd) {
    auto conn = getConnection(fd);
    if (!conn) return;

    std::vector<char> buffer(static_cast<std::size_t>(config_.readBufferSize));
    while (true) {
        ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            std::lock_guard<std::mutex> lock(conn->mutex());
            if (!conn->closed()) {
                conn->appendReadData(buffer.data(), static_cast<std::size_t>(n));
            }
        } else if (n == 0) {
            closeConnection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[error] read failed: " << strerror(errno) << std::endl;
            closeConnection(fd);
            return;
        }
    }

    scheduleIfIdle(conn);
}

void HttpServer::handleWrite(int fd) {
    auto conn = getConnection(fd);
    if (!conn) return;

    while (true) {
        std::string chunk;
        {
            std::lock_guard<std::mutex> lock(conn->mutex());
            if (conn->closed() || !conn->hasWriteData()) {
                break;
            }
            chunk.assign(conn->writeBuffer().data(), conn->writeBuffer().size());
        }

        ssize_t n = write(fd, chunk.data(), chunk.size());
        if (n > 0) {
            std::lock_guard<std::mutex> lock(conn->mutex());
            conn->writeBuffer().erase(0, static_cast<std::size_t>(n));
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        std::cerr << "[error] write failed: " << strerror(errno) << std::endl;
        closeConnection(fd);
        return;
    }

    bool closeAfterWrite = false;
    bool hasPendingRead = false;
    {
        std::lock_guard<std::mutex> lock(conn->mutex());
        conn->setWriteReady(false);
        closeAfterWrite = conn->closeAfterWrite();
        hasPendingRead = conn->hasPendingRead();
    }

    if (closeAfterWrite) {
        closeConnection(fd);
        return;
    }

    epoller_.mod(fd, EPOLLIN | EPOLLET);
    if (hasPendingRead) {
        scheduleIfIdle(conn);
    }
}

void HttpServer::processConnectionTask(int fd) {
    auto conn = getConnection(fd);
    if (!conn) return;

    while (true) {
        std::string rawRequest;
        {
            std::lock_guard<std::mutex> lock(conn->mutex());
            if (conn->closed() || !conn->extractOneRequest(rawRequest)) {
                conn->setProcessing(false);
                return;
            }
        }

        HttpRequest request;
        bool parseOk = request.parse(rawRequest);

        std::string responseText;
        bool closeAfterWrite = false;

        if (!parseOk) {
            responseText =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Content-Length: 11\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Bad Request";
            closeAfterWrite = true;
        } else {
            bool keepAlive = request.isKeepAlive();
            HttpResponse response;
            responseText = response.build(request.getPath(), keepAlive, config_.htmlRoot);
            closeAfterWrite = !keepAlive;
        }

        {
            std::lock_guard<std::mutex> lock(conn->mutex());
            if (conn->closed()) return;
            conn->appendWriteData(responseText);
            conn->setCloseAfterWrite(closeAfterWrite);
        }

        epoller_.mod(fd, EPOLLOUT | EPOLLET);

        if (closeAfterWrite) {
            return;
        }
    }
}

void HttpServer::closeConnection(int fd) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) {
            return;
        }
        conn = it->second;
        connections_.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(conn->mutex());
        conn->markClosed();
    }

    epoller_.del(fd);
    close(fd);
}

std::shared_ptr<Connection> HttpServer::getConnection(int fd) {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    return it->second;
}

void HttpServer::scheduleIfIdle(const std::shared_ptr<Connection>& conn) {
    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(conn->mutex());
        if (!conn->closed() && !conn->processing()) {
            conn->setProcessing(true);
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        int fd = conn->fd();
        workers_.enqueue([this, fd] {
            processConnectionTask(fd);
        });
    }
}
