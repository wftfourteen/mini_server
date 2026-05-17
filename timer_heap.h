#ifndef TIMER_HEAP_H
#define TIMER_HEAP_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

class TimerHeap {
public:
    typedef std::chrono::steady_clock Clock;
    typedef Clock::time_point TimePoint;

    void addOrUpdate(int fd, TimePoint expiresAt);
    void remove(int fd);
    std::vector<int> popExpired(TimePoint now);
    bool empty() const { return latest_.empty(); }
    std::size_t size() const { return latest_.size(); }

private:
    struct Node {
        TimePoint expiresAt;
        int fd;
        std::size_t version;
    };

    struct Later {
        bool operator()(const Node& lhs, const Node& rhs) const {
            return lhs.expiresAt > rhs.expiresAt;
        }
    };

    struct Entry {
        TimePoint expiresAt;
        std::size_t version;
    };

    std::priority_queue<Node, std::vector<Node>, Later> heap_;
    std::unordered_map<int, Entry> latest_;
};

#endif // TIMER_HEAP_H
