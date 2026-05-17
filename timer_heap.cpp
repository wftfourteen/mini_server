#include "timer_heap.h"

void TimerHeap::addOrUpdate(int fd, TimePoint expiresAt) {
    std::size_t version = 1;
    auto it = latest_.find(fd);
    if (it != latest_.end()) {
        version = it->second.version + 1;
    }
    latest_[fd] = {expiresAt, version};
    heap_.push({expiresAt, fd, version});
}

void TimerHeap::remove(int fd) {
    latest_.erase(fd);
}

std::vector<int> TimerHeap::popExpired(TimePoint now) {
    std::vector<int> expired;
    while (!heap_.empty() && heap_.top().expiresAt <= now) {
        Node node = heap_.top();
        heap_.pop();
        auto it = latest_.find(node.fd);
        if (it == latest_.end() ||
            it->second.version != node.version ||
            it->second.expiresAt != node.expiresAt) {
            continue;
        }
        expired.push_back(node.fd);
        latest_.erase(it);
    }
    return expired;
}
