#pragma once
#if defined(PLATFORM_WINDOWS) || (!defined(PLATFORM_MACOS) && !defined(PLATFORM_LINUX))

#include "core/EventLoop.hpp"
#include <unordered_map>
#include <deque>

// ---------------------------------------------------------------------------
// SelectEventLoop — portable fallback using select(2).
// Supports Windows (where kqueue/epoll are unavailable).
// Limited to FD_SETSIZE file descriptors (~1024 on most platforms).
// ---------------------------------------------------------------------------

class SelectEventLoop final : public EventLoop
{
public:
    SelectEventLoop();
    ~SelectEventLoop() override;

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
    };

    bool running_ = false;
    std::unordered_map<int, FdHandlers> handlers_;
    std::deque<Handler>                 pending_;

#ifdef PLATFORM_WINDOWS
    SOCKET wakeup_recv_ = INVALID_SOCKET;
    SOCKET wakeup_send_ = INVALID_SOCKET;
    void create_wakeup_pair();
    void wakeup();
    void drain_wakeup();
#else
    int wakeup_[2] = {-1, -1};

    void wakeup()
    {
        char b=1;
        ::write(wakeup_[1], &b, 1);
    }

    void drain_wakeup()
    {
        char b[64];
        ::read(wakeup_[0], b, sizeof(b));
    }
#endif
};

#endif
