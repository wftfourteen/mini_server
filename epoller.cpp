#include "epoller.h"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

Epoller::Epoller()
    : epfd_(epoll_create1(0)) {
    if (epfd_ == -1) {
        std::cerr << "[error] epoll_create1 failed: " << strerror(errno) << std::endl;
    }
}

Epoller::~Epoller() {
    if (epfd_ != -1) {
        close(epfd_);
    }
}

bool Epoller::add(int fd, std::uint32_t events) const {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cerr << "[error] epoll_ctl ADD failed fd=" << fd
                  << ": " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool Epoller::mod(int fd, std::uint32_t events) const {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        std::cerr << "[error] epoll_ctl MOD failed fd=" << fd
                  << ": " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void Epoller::del(int fd) const {
    if (epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        std::cerr << "[error] epoll_ctl DEL failed fd=" << fd
                  << ": " << strerror(errno) << std::endl;
    }
}

int Epoller::wait(std::vector<epoll_event>& events, int timeoutMs) const {
    return epoll_wait(epfd_, events.data(), static_cast<int>(events.size()), timeoutMs);
}
