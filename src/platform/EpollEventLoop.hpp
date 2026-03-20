#pragma once
#ifdef PLATFORM_LINUX

#include "core/EventLoop.hpp"
#include <unordered_map>
#include <deque>

// ---------------------------------------------------------------------------
// EpollEventLoop — Linux implementation using epoll(7).
// Level-triggered (no EPOLLET flag) for simplicity.
// ---------------------------------------------------------------------------

class EpollEventLoop final : public EventLoop
{
public:
    EpollEventLoop();
    ~EpollEventLoop() override;

    void watch_read(int fd, Handler handler) override;
    void unwatch_read(int fd) override;
    void watch_write(int fd, Handler handler) override;
    void unwatch_write(int fd) override;
    void unwatch(int fd) override;

    void post(Handler handler) override;
    void run() override;
    void stop() override;

private:
    struct FdHandlers
    {
        Handler read_handler;
        Handler write_handler;
        uint32_t events = 0; // current epoll events mask
    };

    void epoll_update(int fd, FdHandlers& h);

    int  epfd_      = -1;
    int  wakeup_[2] = {-1, -1};
    bool running_   = false;

    std::unordered_map<int, FdHandlers> handlers_;
    std::deque<Handler>                 pending_;
};

#endif // PLATFORM_LINUX
