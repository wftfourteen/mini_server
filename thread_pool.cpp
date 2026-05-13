#include "thread_pool.h"

ThreadPool::ThreadPool(std::size_t threadCount)
    : threadCount_(threadCount)
    , stopping_(false) {}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    for (std::size_t i = 0; i < threadCount_; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::enqueue(const std::function<void()>& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        tasks_.push(task);
    }
    cv_.notify_one();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = tasks_.front();
            tasks_.pop();
        }
        task();
    }
}
