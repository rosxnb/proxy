#pragma once

#ifdef PLATFORM_WINDOWS
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using socket_t = SOCKET;
   static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
   using socket_t = int;
   static constexpr socket_t kInvalidSocket = -1;
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <expected>
#include <span>


enum class IoError
{
    WouldBlock,
    Closed,
    Fatal
};

/**
 * @struct PeerAddr
 * @brief Represents a network endpoint (IP address and port).
 */
struct PeerAddr
{
    std::string ip;       ///< The string representation of the IP address (IPv4 or IPv6).
    uint16_t    port = 0; ///< The network port number in host byte order.

    /**
     * @brief Formats the address as a standard "IP:port" string.
     * @return A string formatted as "ip:port".
     */
    std::string to_string() const { return ip + ":" + std::to_string(port); }
};

/**
 * @class Socket
 * @brief A cross-platform, RAII-enforced wrapper for a non-blocking TCP socket.
 * * @details Ownership of the underlying OS socket handle is unique, mirroring `std::unique_ptr`. 
 * It cannot be copied, only moved. The destructor guarantees that the socket will be 
 * closed automatically to prevent resource leaks.
 */
class Socket
{
public:
    /**
     * @brief Constructs an empty, invalid Socket.
     */
    Socket() : fd_(kInvalidSocket) {}

    /**
     * @brief Constructs a Socket taking ownership of an existing OS socket handle.
     * @param fd The raw file descriptor or SOCKET handle to wrap.
     */
    explicit Socket(socket_t fd) : fd_(fd) {}

    // Not copyable — unique ownership
    Socket(Socket const&)            = delete;
    Socket& operator=(Socket const&) = delete;

    /**
     * @brief Move constructor. Transfers ownership of the socket handle.
     * @param o The source Socket to move from. It will be left in an invalid state.
     */
    Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = kInvalidSocket; }

    /**
     * @brief Move assignment operator. 
     * @details Safely closes any currently owned socket before taking ownership of the new one.
     * @param o The source Socket to move from.
     * @return Reference to this Socket.
     */
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) {
            close_fd();
            fd_ = o.fd_;
            o.fd_ = kInvalidSocket;
        }

        return *this;
    }

    /**
     * @brief Destructor. Closes the underlying OS socket if it is valid.
     */
    ~Socket() { close_fd(); }

    /**
     * @brief Creates and binds a non-blocking TCP listening socket.
     * @param host The local IP address or interface to bind to (e.g., "0.0.0.0" or "127.0.0.1").
     * @param port The local port to listen on.
     * @param backlog The maximum length to which the queue of pending connections may grow.
     * @return A valid Socket on success, or std::nullopt on failure (e.g., address in use).
     */
    static std::optional<Socket> make_listener(std::string const& host,
                                               uint16_t port,
                                               int backlog = 128);

    /**
     * @brief Creates a non-blocking TCP socket and initiates a connection.
     * * @warning Because the socket is non-blocking, the connection will likely not be 
     * established by the time this function returns. The caller must register the socket 
     * with an EventLoop to watch for writability. Once writable, check `getsockopt` with 
     * `SO_ERROR` to confirm if the connection succeeded or failed.
     * * @param host The target IP address or hostname to connect to.
     * @param port The target port number.
     * @return A wrapped Socket immediately, or std::nullopt if the initial socket creation failed.
     */
    static std::optional<Socket> make_connector(std::string const& host,
                                                 uint16_t port);

    /**
     * @brief Attempts to accept a single pending connection from the listening queue.
     * @param peer Optional pointer to a PeerAddr struct to populate with the client's address.
     * @return A newly connected Socket, or std::nullopt if no connections are pending (EWOULDBLOCK).
     */
    std::optional<Socket> accept(PeerAddr* peer = nullptr) const;

    /**
     * @brief Attempts to read data from the socket into the provided buffer.
     * @param buf Pointer to the destination buffer.
     * @param len Maximum number of bytes to read.
     * @return 
     * - `> 0`: The number of bytes successfully read.
     * - `  0`: The peer gracefully closed the connection (EOF).
     * - `< 0`: No data available right now (EAGAIN / EWOULDBLOCK). Call again later.
     */
    std::expected<size_t, IoError> read(std::span<uint8_t> buffer) const;

    /**
     * @brief Attempts to write data to the socket from the provided buffer.
     * @param buf Pointer to the source data buffer.
     * @param len Number of bytes to write.
     * @return The number of bytes successfully written, or -1 on error (including EAGAIN).
     */
    std::expected<size_t, IoError> write(std::span<uint8_t const> buffer) const;

    /**
     * @brief Configures the socket for non-blocking I/O operations.
     */
    void set_nonblocking();

    /**
     * @brief Disables the Nagle algorithm (TCP_NODELAY), forcing data to be sent immediately.
     */
    void set_nodelay();

    /**
     * @brief Enables SO_REUSEADDR, allowing rapid rebinding to the port after a crash/restart.
     */
    void set_reuseaddr();

    /**
     * @brief Enables SO_REUSEPORT, allowing multiple sockets to bind to the same port.
     */
    void set_reuseport();

    /**
     * @brief Retrieves the IP address and port of the remotely connected peer.
     * @return A PeerAddr struct containing the remote endpoint details.
     */
    PeerAddr peer_addr() const;

    /**
     * @brief Retrieves the local IP address and port this socket is bound to.
     * @return A PeerAddr struct containing the local endpoint details.
     */
    PeerAddr local_addr() const;

    /**
     * @brief Checks if the socket currently holds a valid OS file descriptor.
     * @return true if valid, false if kInvalidSocket.
     */
    bool valid() const { return fd_ != kInvalidSocket; }

    /**
     * @brief Gets the raw underlying OS socket handle.
     * @return The file descriptor or SOCKET handle.
     */
    socket_t fd() const { return fd_; }

    /**
     * @brief Releases ownership of the OS socket handle.
     * @warning The caller assumes full responsibility for calling close() on the returned descriptor.
     * @return The raw file descriptor, leaving this Socket in an invalid state.
     */
    socket_t release() { socket_t tmp = fd_; fd_ = kInvalidSocket; return tmp; }

private:
    /**
     * @brief Internally closes the file descriptor using the appropriate platform-specific call.
     */
    void close_fd();

    socket_t fd_; ///< The underlying OS socket handle.
};
