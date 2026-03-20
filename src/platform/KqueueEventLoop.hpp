#pragma once
#ifdef PLATFORM_MACOS

#include "core/EventLoop.hpp"
#include <unordered_map>
#include <deque>
#include <mutex>

/**
 * @class KqueueEventLoop
 * @brief macOS / BSD implementation of the EventLoop using kqueue(2) and kevent(2).
 *
 * @details
 * This event loop utilizes **level-triggered** semantics. This means that as long as 
 * there is data remaining in the kernel's receive buffer (for reads), or space available 
 * in the kernel's send buffer (for writes), the event will repeatedly fire on loop iterations.
 * * This design specifically simplifies proxy relay logic: if a read interceptor pauses 
 * processing or a buffer fills up, we don't have to strictly drain the socket until `EAGAIN` 
 * every single time. We can just return to the loop and it will fire again later.
 * * Thread Safety: File descriptor watches (`watch_*` / `unwatch_*`) assume they are called 
 * from the same thread running the loop. The `post()` and `stop()` methods, however, are 
 * thread-safe and use a hidden pipe (`wakeup_`) to interrupt the blocking `kevent` call.
 */
class KqueueEventLoop final : public EventLoop
{
public:
    KqueueEventLoop();
    ~KqueueEventLoop() override;

    /**
     * @brief Registers a callback to be invoked when the given file descriptor is ready to be read.
     * @param fd The socket file descriptor.
     * @param handler The callback function to execute.
     */
    void watch_read(int fd, Handler handler) override;

    /**
     * @brief Stops monitoring the file descriptor for read events.
     * @param fd The socket file descriptor.
     */
    void unwatch_read(int fd) override;

    /**
     * @brief Registers a callback to be invoked when the given file descriptor is ready for writing.
     * @param fd The socket file descriptor.
     * @param handler The callback function to execute.
     */
    void watch_write(int fd, Handler handler) override;

    /**
     * @brief Stops monitoring the file descriptor for write events.
     * @param fd The socket file descriptor.
     */
    void unwatch_write(int fd) override;

    /**
     * @brief Completely removes the file descriptor from the event loop (stops watching read and write).
     * @param fd The socket file descriptor.
     */
    void unwatch(int fd) override;

    /**
     * @brief Thread-safely queues a task to be executed on the event loop's main thread.
     * @details Writes a byte to the `wakeup_` pipe to wake up the blocking `kevent` call immediately.
     * @param handler The task to execute.
     */
    void post(Handler handler) override;

    /**
     * @brief Starts the blocking event loop. Will not return until `stop()` is called.
     */
    void run() override;

    /**
     * @brief Thread-safely signals the event loop to terminate.
     */
    void stop() override;

private:
    /**
     * @struct FdHandlers
     * @brief Stores the read and write callbacks for a specific file descriptor.
     */
    struct FdHandlers
    {
        Handler read_handler;
        Handler write_handler;
    };

    /**
     * @brief Helper to configure and register an EV_SET with the underlying kqueue.
     * @param fd The file descriptor to modify.
     * @param filter The kqueue filter (e.g., EVFILT_READ, EVFILT_WRITE).
     * @param flags The kqueue action flags (e.g., EV_ADD, EV_DELETE, EV_ENABLE, EV_DISABLE).
     */
    void apply_kevent(int fd, int16_t filter, uint16_t flags);

private:
    int kq_        = -1;         ///< The kqueue file descriptor.
    int wakeup_[2] = {-1, -1};   ///< Pipe for `stop()` and `post()`. [0]=read, [1]=write.
    bool running_  = false;      ///< Flag to control the main loop execution.

    // Note: If max concurrent connections are known and small, replacing this 
    // unordered_map with a flat `std::vector<FdHandlers>` indexed directly by `fd` 
    // is a highly recommended optimization for massive throughput.
    std::unordered_map<int, FdHandlers> handlers_; 
    
    std::deque<Handler> pending_; ///< Queue of tasks scheduled via `post()`.
    std::mutex pending_mutex_;
};

#endif // PLATFORM_MACOS
