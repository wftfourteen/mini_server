#ifndef LOGGER_H
#define LOGGER_H

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class Logger {
public:
    static Logger& instance();

    void init(const std::string& filepath);
    void shutdown();
    void access(const std::string& clientIp,
                const std::string& method,
                const std::string& path,
                int statusCode,
                std::size_t bytesSent);
    void info(const std::string& message);

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void enqueue(const std::string& line);
    void workerLoop();
    static std::string now();

    std::string filepath_;
    bool running_;
    std::queue<std::string> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
};

#endif // LOGGER_H
