#ifdef PLATFORM_LINUX

#include "platform/EpollEventLoop.hpp"
#include "core/Logger.hpp"
#include "core/EventLoop.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

static constexpr int kMaxEvents = 64;

EpollEventLoop::EpollEventLoop()
{
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0)
        throw std::runtime_error("epoll_create1() failed");

    if (pipe(wakeup_) != 0)
        throw std::runtime_error("pipe() failed");

    // Register wakeup pipe read-end
    struct epoll_event ev{};
    ev.events   = EPOLLIN;
    ev.data.fd  = wakeup_[0];
    epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeup_[0], &ev);
}

EpollEventLoop::~EpollEventLoop()
{
    if (epfd_ >= 0)
        ::close(epfd_);
    if (wakeup_[0] >= 0)
        ::close(wakeup_[0]);
    if (wakeup_[1] >= 0)
        ::close(wakeup_[1]);
}

void
EpollEventLoop::epoll_update(int fd, FdHandlers& h)
{
    uint32_t want = 0;
    if (h.read_handler)
        want |= EPOLLIN;
    if (h.write_handler)
        want |= EPOLLOUT;
    want |= EPOLLRDHUP | EPOLLERR | EPOLLHUP;

    struct epoll_event ev{};
    ev.events  = want;
    ev.data.fd = fd;

    int op = (h.events == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (want == 0)
        op = EPOLL_CTL_DEL;

    if (epoll_ctl(epfd_, op, fd, &ev) < 0 && errno != ENOENT)
        LOG_WARN("epoll", "epoll_ctl op={} fd={}: {}", op, fd, strerror(errno));

    h.events = want;
}

void
EpollEventLoop::watch_read(int fd, Handler handler)
{
    auto& h = handlers_[fd];
    h.read_handler = std::move(handler);
    epoll_update(fd, h);
}

void
EpollEventLoop::unwatch_read(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
        return;

    it->second.read_handler = nullptr;
    epoll_update(fd, it->second);
    if (!it->second.write_handler)
        handlers_.erase(it);
}

void
EpollEventLoop::watch_write(int fd, Handler handler)
{
    auto& h = handlers_[fd];
    h.write_handler = std::move(handler);
    epoll_update(fd, h);
}

void
EpollEventLoop::unwatch_write(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
        return;

    it->second.write_handler = nullptr;
    epoll_update(fd, it->second);
    if (!it->second.read_handler)
        handlers_.erase(it);
}

void
EpollEventLoop::unwatch(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
        return;

    it->second.read_handler  = nullptr;
    it->second.write_handler = nullptr;
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    handlers_.erase(it);
}

void
EpollEventLoop::post(Handler handler)
{
    pending_.push_back(std::move(handler));
    char byte = 1;
    (void)::write(wakeup_[1], &byte, 1);
}

void
EpollEventLoop::stop()
{
    running_ = false;
    char byte = 0;
    (void)::write(wakeup_[1], &byte, 1);
}

void
EpollEventLoop::run()
{
    running_ = true;
    struct epoll_event events[kMaxEvents];

    while (running_) {
        while (!pending_.empty()) {
            Handler cb = std::move(pending_.front());
            pending_.pop_front();
            cb();
        }
        if (!running_)
            break;

        int n = epoll_wait(epfd_, events, kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERROR("epoll", "epoll_wait(): {}", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == wakeup_[0]) {
                char buf[64];
                (void)::read(fd, buf, sizeof(buf));
                continue;
            }

            auto it = handlers_.find(fd);
            if (it == handlers_.end())
                continue;

            uint32_t ev = events[i].events;
            // Errors / hangups: deliver as readable so the app sees EOF
            if ((ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) && it->second.read_handler)
                it->second.read_handler();
            else {
                if ((ev & EPOLLIN)  && it->second.read_handler)
                    it->second.read_handler();
                if ((ev & EPOLLOUT) && it->second.write_handler)
                    it->second.write_handler();
            }
        }
    }
}

std::unique_ptr<EventLoop>
EventLoop::create()
{
    return std::make_unique<EpollEventLoop>();
}

#endif // PLATFORM_LINUX
