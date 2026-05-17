#include "logger.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger()
    : running_(false) {}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    filepath_ = filepath;
    if (!running_) {
        running_ = true;
        worker_ = std::thread(&Logger::workerLoop, this);
    }
}

void Logger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Logger::access(const std::string& clientIp,
                    const std::string& method,
                    const std::string& path,
                    int statusCode,
                    std::size_t bytesSent) {
    std::ostringstream out;
    out << now() << " ACCESS "
        << clientIp << " "
        << method << " "
        << path << " "
        << statusCode << " "
        << bytesSent;
    enqueue(out.str());
}

void Logger::info(const std::string& message) {
    enqueue(now() + " INFO " + message);
}

void Logger::enqueue(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(line);
    }
    cv_.notify_one();
}

void Logger::workerLoop() {
    while (true) {
        std::queue<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_ || !queue_.empty();
            });
            if (!running_ && queue_.empty()) {
                return;
            }
            batch.swap(queue_);
        }

        std::ofstream out(filepath_.c_str(), std::ios::app);
        while (!batch.empty()) {
            out << batch.front() << "\n";
            batch.pop();
        }
    }
}

std::string Logger::now() {
    std::time_t value = std::time(nullptr);
    std::tm tmValue;
    localtime_r(&value, &tmValue);
    std::ostringstream out;
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return out.str();
}
