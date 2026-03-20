#pragma once

#include <functional>
#include <memory>

/**
 * @class EventLoop
 * @brief The abstract base class for a platform-specific reactor/event loop.
 *
 * @details This interface drives the asynchronous heart of the proxy. Under the hood, 
 * the factory method will instantiate one of the following platform-optimized subclasses:
 * - KqueueEventLoop  (macOS / BSD  -> kqueue / kevent)
 * - EpollEventLoop   (Linux        -> epoll)
 * - SelectEventLoop  (Windows / fallback -> select)
 *
 * @warning Threading constraint: All methods on this class must be called from the 
 * exact same thread that invokes the run() method. It is not inherently thread-safe.
 */
class EventLoop
{
public:
    /**
     * @brief A generic callable handler used for asynchronous event callbacks.
     */
    using Handler = std::function<void()>;

    virtual ~EventLoop() = default;

    /**
     * @brief Registers a callback to be fired when a file descriptor becomes readable.
     * * @note If a read handler is already registered for this fd, this new handler 
     * will completely replace it.
     * * @param fd The file descriptor (socket) to monitor.
     * @param handler The callback to execute when data is available to read.
     */
    virtual void watch_read(int fd, Handler handler) = 0;

    /**
     * @brief Stops monitoring a file descriptor for read events.
     * @param fd The file descriptor to unwatch.
     */
    virtual void unwatch_read(int fd) = 0;

    /**
     * @brief Registers a callback to be fired when a file descriptor becomes writable.
     * * @note You should typically only register a write watch when you actively have 
     * buffered data ready to send. Leaving a write watch active constantly will cause 
     * the event loop to spin needlessly at 100% CPU, as sockets are almost always writable.
     * * @param fd The file descriptor (socket) to monitor.
     * @param handler The callback to execute when the socket buffer has space.
     */
    virtual void watch_write(int fd, Handler handler) = 0;

    /**
     * @brief Stops monitoring a file descriptor for write events.
     * @param fd The file descriptor to unwatch.
     */
    virtual void unwatch_write(int fd) = 0;

    /**
     * @brief Removes ALL active watches (both read and write) for a file descriptor.
     * * @warning This MUST be called before the file descriptor is closed (e.g., before 
     * calling `close(fd)`). Failing to do so can result in undefined behavior or dead 
     * entries in the underlying epoll/kqueue instance.
     * * @param fd The file descriptor to completely unwatch.
     */
    virtual void unwatch(int fd) = 0;

    /**
     * @brief Schedules a callback to run during the next iteration of the event loop.
     * * @details This is highly useful for deferring cleanup tasks (like deleting a connection 
     * object) or breaking deep call chains. 
     * * @note It is completely safe to call post() from within another currently executing handler.
     * * @param handler The callback to defer.
     */
    virtual void post(Handler handler) = 0;

    /**
     * @brief Starts the event loop.
     * * @details This method blocks the calling thread, continually polling for IO events 
     * and executing deferred handlers. It will only return when stop() is explicitly called.
     */
    virtual void run() = 0;

    /**
     * @brief Signals the event loop to terminate.
     * * @details The loop will not exit instantaneously; it will finish processing the 
     * current iteration's events/handlers and then cleanly return from run().
     */
    virtual void stop() = 0;

    /**
     * @brief Creates the optimal EventLoop implementation for the current OS.
     * @return A unique pointer to the instantiated EventLoop (e.g., EpollEventLoop on Linux).
     */
    static std::unique_ptr<EventLoop> create();
};
