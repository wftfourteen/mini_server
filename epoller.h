#ifndef EPOLLER_H
#define EPOLLER_H

#include <cstdint>
#include <vector>

#include <sys/epoll.h>

class Epoller {
public:
    Epoller();
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    bool valid() const { return epfd_ != -1; }
    int fd() const { return epfd_; }

    bool add(int fd, std::uint32_t events) const;
    bool mod(int fd, std::uint32_t events) const;
    void del(int fd) const;
    int wait(std::vector<epoll_event>& events, int timeoutMs) const;

private:
    int epfd_;
};

bool set_nonblocking(int fd);

#endif // EPOLLER_H
