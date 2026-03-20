#ifdef PLATFORM_MACOS

#include "platform/KqueueEventLoop.hpp"
#include "core/Logger.hpp"

#include <sys/event.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <stdexcept>

static constexpr int kMaxEvents = 64;

KqueueEventLoop::KqueueEventLoop()
{
    kq_ = kqueue();
    if (kq_ < 0)
        throw std::runtime_error("kqueue() failed");

    if (pipe(wakeup_) != 0)
        throw std::runtime_error("pipe() failed for wakeup");

    // Set the both end of the pipe to non-blocking
    int flags0 = fcntl(wakeup_[0], F_GETFL, 0);
    fcntl(wakeup_[0], F_SETFL, flags0 | O_NONBLOCK);

    int flags1 = fcntl(wakeup_[1], F_GETFL, 0);
    fcntl(wakeup_[1], F_SETFL, flags1 | O_NONBLOCK);

    // Watch the read end of the wakeup pipe for readability.
    // We store no handler for it — the run() loop handles it directly.
    apply_kevent(wakeup_[0], EVFILT_READ, EV_ADD | EV_ENABLE);
}

KqueueEventLoop::~KqueueEventLoop()
{
    if (kq_ >= 0)
        ::close(kq_);
    if (wakeup_[0] >= 0)
        ::close(wakeup_[0]);
    if (wakeup_[1] >= 0)
        ::close(wakeup_[1]);
}

// ── Internal helper ───────────────────────────────────────────────────────────

void
KqueueEventLoop::apply_kevent(int fd, int16_t filter, uint16_t flags)
{
    struct kevent kev{};
    EV_SET(&kev, static_cast<uintptr_t>(fd), filter, flags, 0, 0, nullptr);
    if (kevent(kq_, &kev, 1, nullptr, 0, nullptr) < 0) {
        // EV_DELETE on a non-registered fd returns ENOENT — silently ignore
        if (errno != ENOENT)
            LOG_WARN("kqueue", "kevent() fd={} filter={} flags={}: {}",
                     fd, filter, flags, strerror(errno));
    }
}

// ── EventLoop interface ───────────────────────────────────────────────────────

void
KqueueEventLoop::watch_read(int fd, Handler handler)
{
    handlers_[fd].read_handler = std::move(handler);
    apply_kevent(fd, EVFILT_READ, EV_ADD | EV_ENABLE);
}

void
KqueueEventLoop::unwatch_read(int fd)
{
    apply_kevent(fd, EVFILT_READ, EV_DELETE);
    auto it = handlers_.find(fd);
    if (it != handlers_.end()) {
        it->second.read_handler = nullptr;
        if (!it->second.write_handler)
            handlers_.erase(it);
    }
}

void
KqueueEventLoop::watch_write(int fd, Handler handler)
{
    handlers_[fd].write_handler = std::move(handler);
    apply_kevent(fd, EVFILT_WRITE, EV_ADD | EV_ENABLE);
}

void
KqueueEventLoop::unwatch_write(int fd)
{
    apply_kevent(fd, EVFILT_WRITE, EV_DELETE);
    auto it = handlers_.find(fd);
    if (it != handlers_.end()) {
        it->second.write_handler = nullptr;
        if (!it->second.read_handler)
            handlers_.erase(it);
    }
}

void
KqueueEventLoop::unwatch(int fd)
{
    // EV_DELETE silently fails if filter wasn't registered, which is fine.
    apply_kevent(fd, EVFILT_READ,  EV_DELETE);
    apply_kevent(fd, EVFILT_WRITE, EV_DELETE);
    handlers_.erase(fd);
}

void
KqueueEventLoop::post(Handler handler)
{
    pending_.push_back(std::move(handler));

    // Wake up the blocked kevent() call.
    char byte = 1;
    ssize_t written = ::write(wakeup_[1], &byte, 1);
    if (written < 0 && errno == EAGAIN) {
        LOG_DEBUG("kqueue", "Wakeup pipe full, but loop will run anyway");
    }
}

void
KqueueEventLoop::stop()
{
    running_ = false;
    char byte = 0;
    (void)::write(wakeup_[1], &byte, 1);
}

void
KqueueEventLoop::run()
{
    running_ = true;
    struct kevent events[kMaxEvents];

    while (running_) {
        // Drain and run deferred callback first
        while (!pending_.empty()) {
            Handler cb = std::move(pending_.front());
            pending_.pop_front();
            cb();
        }

        if (!running_)
            break;

        // Block until at least one event is ready
        int n = kevent(kq_, nullptr, 0, events, kMaxEvents, nullptr);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERROR("kqueue", "kevent() returned error: {}", strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            auto const& ev = events[i];
            int fd = static_cast<int>(ev.ident);

            // ── Wakeup pipe ───────────────────────────────────────────────
            if (fd == wakeup_[0]) {
                char buf[64];
                (void)::read(fd, buf, sizeof(buf));  // drain
                continue;
            }

            // ── Error / EOF ───────────────────────────────────────────────
            // EV_EOF: the remote side has close. Deliver as a readable
            // event so that application can detect EOF via recv()==0
            if (ev.flags & EV_ERROR) {
                LOG_WARN("kqueue", "EV_ERROR on fd={}: {}", fd, strerror(static_cast<int>(ev.data)));
                // fall through to handler so the application can close
            }

            // ── Dispatch ───────────────────────────────────────────────────
            auto it = handlers_.find(fd);
            if (it == handlers_.end())
                continue;

            if (ev.filter == EVFILT_READ && it->second.read_handler) {
                it->second.read_handler();
            } else if (ev.filter == EVFILT_WRITE && it->second.write_handler) {
                it->second.write_handler();
            }
        }
    }
}

// ── EventLoop factory (macOS path) ───────────────────────────────────────────
#include "core/EventLoop.hpp"

std::unique_ptr<EventLoop>
EventLoop::create()
{
    return std::make_unique<KqueueEventLoop>();
}

#endif // PLATFORM_MACOS
