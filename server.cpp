#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <cctype>

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http_request.h"
#include "http_response.h"

static const int PORT = 8080;
static const int MAX_EVENTS = 1024;
static const int READ_BUFFER = 8192;
static const std::string HTML_ROOT = "webroot";
static const int WORKER_THREADS = 4;

struct Connection {
    int fd;
    std::string readBuf;
    std::string writeBuf;
    bool writeReady;
    bool closeAfterWrite;
    bool processing;
    std::mutex mtx;

    explicit Connection(int fd_)
        : fd(fd_), writeReady(false), closeAfterWrite(false), processing(false) {}
};

static std::unordered_map<int, std::shared_ptr<Connection>> g_connections;
static std::mutex g_connMutex;
static int g_epfd = -1;

static std::queue<int> g_taskQueue;
static std::mutex g_taskMutex;
static std::condition_variable g_taskCv;
static bool g_stopping = false;
static std::vector<std::thread> g_workers;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void epoll_add(int epfd, int fd, uint32_t events) {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cerr << "[错误] epoll_ctl ADD 失败 fd=" << fd
                  << ": " << strerror(errno) << std::endl;
    }
}

void epoll_mod(int epfd, int fd, uint32_t events) {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        std::cerr << "[错误] epoll_ctl MOD 失败 fd=" << fd
                  << ": " << strerror(errno) << std::endl;
    }
}

void epoll_del(int epfd, int fd) {
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        std::cerr << "[错误] epoll_ctl DEL 失败 fd=" << fd
                  << ": " << strerror(errno) << std::endl;
    }
    close(fd);
}

std::shared_ptr<Connection> get_connection(int fd) {
    std::lock_guard<std::mutex> lock(g_connMutex);
    auto it = g_connections.find(fd);
    if (it == g_connections.end()) {
        return nullptr;
    }
    return it->second;
}

void close_connection(int epfd, int fd) {
    size_t connSize = 0;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        g_connections.erase(fd);
        connSize = g_connections.size();
    }
    std::cout << "[关闭] 连接关闭 fd=" << fd << "  当前连接数: " << connSize << std::endl;
    epoll_del(epfd, fd);
}

size_t parse_content_length(const std::string& headersText) {
    const std::string key = "Content-Length:";
    size_t pos = headersText.find(key);
    if (pos == std::string::npos) return 0;

    pos += key.size();
    while (pos < headersText.size() &&
           (headersText[pos] == ' ' || headersText[pos] == '\t')) {
        ++pos;
    }

    size_t end = pos;
    while (end < headersText.size() && std::isdigit(static_cast<unsigned char>(headersText[end]))) {
        ++end;
    }

    if (end == pos) return 0;
    return static_cast<size_t>(std::stoul(headersText.substr(pos, end - pos)));
}

bool extract_one_request(std::string& readBuf, std::string& rawRequest) {
    size_t headerEnd = readBuf.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    size_t headersLen = headerEnd + 4;
    std::string headersText = readBuf.substr(0, headersLen);
    size_t contentLength = parse_content_length(headersText);

    size_t totalLen = headersLen + contentLength;
    if (readBuf.size() < totalLen) return false;

    rawRequest = readBuf.substr(0, totalLen);
    readBuf.erase(0, totalLen);
    return true;
}

void enqueue_task(int fd) {
    {
        std::lock_guard<std::mutex> lock(g_taskMutex);
        g_taskQueue.push(fd);
    }
    g_taskCv.notify_one();
}

void process_connection_task(int fd) {
    auto conn = get_connection(fd);
    if (!conn) return;

    while (true) {
        std::string rawRequest;
        {
            std::lock_guard<std::mutex> lock(conn->mtx);
            if (!extract_one_request(conn->readBuf, rawRequest)) {
                conn->processing = false;
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
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: 11\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Bad Request";
            closeAfterWrite = true;
        } else {
            bool keepAlive = request.isKeepAlive();
            HttpResponse response;
            responseText = response.build(request.getPath(), keepAlive, HTML_ROOT);
            closeAfterWrite = !keepAlive;
        }

        {
            std::lock_guard<std::mutex> lock(conn->mtx);
            conn->writeBuf.append(responseText);
            conn->writeReady = !conn->writeBuf.empty();
            conn->closeAfterWrite = closeAfterWrite;
        }

        epoll_mod(g_epfd, fd, EPOLLOUT | EPOLLET);

        if (closeAfterWrite) {
            return;
        }
    }
}

void worker_loop() {
    while (true) {
        int fd = -1;
        {
            std::unique_lock<std::mutex> lock(g_taskMutex);
            g_taskCv.wait(lock, [] {
                return g_stopping || !g_taskQueue.empty();
            });

            if (g_stopping && g_taskQueue.empty()) {
                return;
            }

            fd = g_taskQueue.front();
            g_taskQueue.pop();
        }

        process_connection_task(fd);
    }
}

void handle_accept(int epfd, int listen_fd) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (true) {
        int conn_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (conn_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[错误] accept 失败: " << strerror(errno) << std::endl;
            return;
        }

        std::cout << "[新连接] " << inet_ntoa(client_addr.sin_addr)
                  << ":" << ntohs(client_addr.sin_port)
                  << "  fd=" << conn_fd << std::endl;

        set_nonblocking(conn_fd);

        {
            std::lock_guard<std::mutex> lock(g_connMutex);
            g_connections[conn_fd] = std::make_shared<Connection>(conn_fd);
        }

        epoll_add(epfd, conn_fd, EPOLLIN | EPOLLET);
    }
}

void handle_read(int epfd, int fd) {
    (void)epfd;
    auto conn = get_connection(fd);
    if (!conn) return;

    char buf[READ_BUFFER];
    while (true) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n > 0) {
            std::lock_guard<std::mutex> lock(conn->mtx);
            conn->readBuf.append(buf, n);
        } else if (n == 0) {
            std::cout << "[关闭] 客户端断开 fd=" << fd << std::endl;
            close_connection(g_epfd, fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[错误] read 失败: " << strerror(errno) << std::endl;
            close_connection(g_epfd, fd);
            return;
        }
    }

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(conn->mtx);
        if (!conn->processing) {
            conn->processing = true;
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        enqueue_task(fd);
    }
}

void handle_write(int epfd, int fd) {
    auto conn = get_connection(fd);
    if (!conn) return;

    bool writeReady = false;
    {
        std::lock_guard<std::mutex> lock(conn->mtx);
        writeReady = conn->writeReady;
    }

    if (!writeReady) {
        epoll_mod(epfd, fd, EPOLLIN | EPOLLET);
        return;
    }

    while (true) {
        std::lock_guard<std::mutex> lock(conn->mtx);
        if (conn->writeBuf.empty()) {
            break;
        }

        ssize_t n = write(fd, conn->writeBuf.data(), conn->writeBuf.size());
        if (n > 0) {
            conn->writeBuf.erase(0, static_cast<size_t>(n));
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        std::cerr << "[错误] write 失败: " << strerror(errno) << std::endl;
        close_connection(epfd, fd);
        return;
    }

    bool closeAfterWrite = false;
    bool hasPendingRead = false;
    {
        std::lock_guard<std::mutex> lock(conn->mtx);
        conn->writeReady = false;
        closeAfterWrite = conn->closeAfterWrite;
        hasPendingRead = !conn->readBuf.empty();
    }

    if (closeAfterWrite) {
        close_connection(epfd, fd);
        return;
    }

    epoll_mod(epfd, fd, EPOLLIN | EPOLLET);

    if (hasPendingRead) {
        bool shouldSchedule = false;
        {
            std::lock_guard<std::mutex> lock(conn->mtx);
            if (!conn->processing) {
                conn->processing = true;
                shouldSchedule = true;
            }
        }
        if (shouldSchedule) enqueue_task(fd);
    }
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        std::cerr << "[错误] socket 创建失败: " << strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "[错误] bind 失败: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) == -1) {
        std::cerr << "[错误] listen 失败: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    set_nonblocking(listen_fd);

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        std::cerr << "[错误] epoll_create1 失败: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    g_epfd = epfd;
    epoll_add(epfd, listen_fd, EPOLLIN | EPOLLET);

    for (int i = 0; i < WORKER_THREADS; ++i) {
        g_workers.emplace_back(worker_loop);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  MiniServer v3 —— 线程池 + Keep-Alive" << std::endl;
    std::cout << "  监听端口: " << PORT << std::endl;
    std::cout << "  静态根目录: " << HTML_ROOT << std::endl;
    std::cout << "  工作线程数: " << WORKER_THREADS << std::endl;
    std::cout << "========================================" << std::endl;

    epoll_event events[MAX_EVENTS];

    while (true) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::cerr << "[错误] epoll_wait 失败: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd) {
                handle_accept(epfd, listen_fd);
            } else if (ev & (EPOLLHUP | EPOLLERR)) {
                std::cout << "[异常] fd=" << fd << " 连接异常" << std::endl;
                close_connection(epfd, fd);
            } else {
                if (ev & EPOLLIN) {
                    handle_read(epfd, fd);
                }
                if (ev & EPOLLOUT) {
                    handle_write(epfd, fd);
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_taskMutex);
        g_stopping = true;
    }
    g_taskCv.notify_all();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        for (auto& pair : g_connections) {
            close(pair.first);
        }
        g_connections.clear();
    }

    close(epfd);
    close(listen_fd);
    return 0;
}
