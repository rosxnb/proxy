#if defined(PLATFORM_WINDOWS) || (!defined(PLATFORM_MACOS) && !defined(PLATFORM_LINUX))

#include "platform/SelectEventLoop.hpp"
#include "core/Logger.hpp"
#include "core/EventLoop.hpp"

#ifdef PLATFORM_WINDOWS
#  include <winsock2.h>
#else
#  include <sys/select.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
#  include <cstring>
#endif

#include <stdexcept>
#include <algorithm>

SelectEventLoop::SelectEventLoop()
{
#ifdef PLATFORM_WINDOWS
    create_wakeup_pair();
#else
    if (pipe(wakeup_) != 0)
        throw std::runtime_error("pipe() failed");
    int flags = fcntl(wakeup_[0], F_GETFL, 0);
    fcntl(wakeup_[0], F_SETFL, flags | O_NONBLOCK);
#endif
}

SelectEventLoop::~SelectEventLoop()
{
#ifdef PLATFORM_WINDOWS
    if (wakeup_recv_ != INVALID_SOCKET)
        closesocket(wakeup_recv_);
    if (wakeup_send_ != INVALID_SOCKET)
        closesocket(wakeup_send_);
#else
    if (wakeup_[0] >= 0)
        ::close(wakeup_[0]);
    if (wakeup_[1] >= 0)
        ::close(wakeup_[1]);
#endif
}

#ifdef PLATFORM_WINDOWS
void
SelectEventLoop::create_wakeup_pair()
{
    // On Windows we emulate a self-pipe with a local loopback TCP socket pair.
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);

    int len = sizeof(addr);
    getsockname(srv, (sockaddr*)&addr, &len);

    wakeup_send_ = socket(AF_INET, SOCK_STREAM, 0);
    connect(wakeup_send_, (sockaddr*)&addr, sizeof(addr));

    wakeup_recv_ = accept(srv, nullptr, nullptr);
    closesocket(srv);

    u_long mode = 1;
    ioctlsocket(wakeup_recv_, FIONBIO, &mode);
}

void
SelectEventLoop::wakeup()
{
    char b = 1;
    send(wakeup_send_, &b, 1, 0);
}

void
SelectEventLoop::drain_wakeup()
{
    char buf[64];
    recv(wakeup_recv_, buf, sizeof(buf), 0);
}
#endif

void
SelectEventLoop::watch_read(int fd, Handler h)
{
    handlers_[fd].read_handler = std::move(h);
}

void
SelectEventLoop::watch_write(int fd, Handler h)
{
    handlers_[fd].write_handler = std::move(h);
}

void
SelectEventLoop::unwatch_read(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
        return;

    it->second.read_handler = nullptr;
    if (!it->second.write_handler)
        handlers_.erase(it);
}

void
SelectEventLoop::unwatch_write(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
        return;

    it->second.write_handler = nullptr;
    if (!it->second.read_handler)
        handlers_.erase(it);
}

void
SelectEventLoop::unwatch(int fd)
{
    handlers_.erase(fd);
}

void
SelectEventLoop::post(Handler h)
{
    pending_.push_back(std::move(h));
    wakeup();
}

void
SelectEventLoop::stop()
{
    running_ = false;
    wakeup();
}

void
SelectEventLoop::run()
{
    running_ = true;

    while (running_) {
        while (!pending_.empty()) {
            Handler cb = std::move(pending_.front());
            pending_.pop_front();
            cb();
        }
        if (!running_)
            break;

        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = 0;

#ifdef PLATFORM_WINDOWS
        FD_SET(wakeup_recv_, &rfds);
        int wakeup_fd = static_cast<int>(wakeup_recv_);
        maxfd = wakeup_fd;
#else
        FD_SET(wakeup_[0], &rfds);
        maxfd = wakeup_[0];
#endif

        for (auto& [fd, h] : handlers_) {
            if (h.read_handler) {
                FD_SET(fd, &rfds);
                maxfd = std::max(maxfd, fd);
            }

            if (h.write_handler) {
                FD_SET(fd, &wfds);
                maxfd = std::max(maxfd, fd);
            }
        }

        int n = select(maxfd + 1, &rfds, &wfds, nullptr, nullptr);
        if (n <= 0)
            continue;

#ifdef PLATFORM_WINDOWS
        if (FD_ISSET(wakeup_recv_, &rfds)) {
            drain_wakeup();
            continue;
        }
#else
        if (FD_ISSET(wakeup_[0], &rfds)) {
            drain_wakeup();
            continue;
        }
#endif

        // Snapshot keys to avoid iterator invalidation if callbacks call unwatch
        std::vector<int> fds;
        fds.reserve(handlers_.size());
        for (auto& [fd, _] : handlers_)
            fds.push_back(fd);

        for (int fd : fds) {
            auto it = handlers_.find(fd);
            if (it == handlers_.end())
                continue;

            if (FD_ISSET(fd, &rfds) && it->second.read_handler)
                it->second.read_handler();

            it = handlers_.find(fd); // re-find: read handler may have unwatched
            if (it == handlers_.end())
                continue;

            if (FD_ISSET(fd, &wfds) && it->second.write_handler)
                it->second.write_handler();
        }
    }
}

std::unique_ptr<EventLoop>
EventLoop::create()
{
    return std::make_unique<SelectEventLoop>();
}

#endif
