#include "core/Socket.hpp"
#include "core/Logger.hpp"

#include <cstring>
#include <cassert>

#ifdef PLATFORM_WINDOWS
#  pragma comment(lib, "ws2_32.lib")
   namespace {
       struct WsaInit {
           WsaInit()  { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); }
           ~WsaInit() { WSACleanup(); }
       } _wsa;
   }
#endif

// ── Portable helpers ──────────────────────────────────────────────────────────

static bool
would_block()
{
#ifdef PLATFORM_WINDOWS
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool
in_progress()
{
#ifdef PLATFORM_WINDOWS
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}

static PeerAddr
parse_address(sockaddr_storage const& addr)
{
    PeerAddr pa;
    char ip[INET6_ADDRSTRLEN] = {};

    if (addr.ss_family == AF_INET) {
        auto* s = reinterpret_cast<sockaddr_in const*>(&addr);
        inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));
        pa.ip   = ip;
        pa.port = ntohs(s->sin_port);
    }
    else if (addr.ss_family == AF_INET6) {
        auto* s = reinterpret_cast<sockaddr_in6 const*>(&addr);
        inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip));
        pa.ip   = ip;
        pa.port = ntohs(s->sin6_port);
    }

    return pa;
}

// ── Socket factory ────────────────────────────────────────────────────────────

std::optional<Socket>
Socket::make_listener(std::string const& host,
                      uint16_t port,
                      int backlog)
{
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    std::string port_str = std::to_string(port);
    char const* h = host.empty() ? nullptr : host.c_str();

    if (getaddrinfo(h, port_str.c_str(), &hints, &res) != 0 || !res) {
        LOG_ERROR("socket", "getaddrinfo failed for {}:{}", host.c_str(), port);
        return std::nullopt;
    }

    socket_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == kInvalidSocket) {
        LOG_ERROR("socket", "socket() failed");
        freeaddrinfo(res);
        return std::nullopt;
    }

    Socket s(fd);
    s.set_reuseaddr();
    s.set_reuseport();
    s.set_nonblocking();

    if (::bind(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
        LOG_ERROR("socket", "bind() failed on {}:{}: {}",
                  host.c_str(), port, strerror(errno));
        freeaddrinfo(res);
        return std::nullopt;
    }
    freeaddrinfo(res);

    if (::listen(fd, backlog) != 0) {
        LOG_ERROR("socket", "listen() failed");
        return std::nullopt;
    }

    LOG_INFO("socket", "Listener ready on {}:{} (fd={})",
             host.empty() ? "0.0.0.0" : host.c_str(), port, static_cast<int>(fd));
    return s;
}

std::optional<Socket>
Socket::make_connector(std::string const& host,
                       uint16_t port)
{
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        LOG_ERROR("socket", "getaddrinfo failed for {}:{}", host.c_str(), port);
        return std::nullopt;
    }

    socket_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == kInvalidSocket) {
        LOG_ERROR("socket", "socket() failed");
        freeaddrinfo(res);
        return std::nullopt;
    }

    Socket s(fd);
    s.set_nonblocking();
    s.set_nodelay();

    int ret = ::connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    freeaddrinfo(res);

    if (ret != 0 && !in_progress()) {
        LOG_ERROR("socket", "connect() failed immediately to {}:{}: {}",
                  host.c_str(), port, strerror(errno));
        return std::nullopt;
    }
    // ret == 0    → connected immediately (loopback is common)
    // EINPROGRESS → caller watches for writability

    return s;
}

// ── Instance methods ──────────────────────────────────────────────────────────

std::optional<Socket>
Socket::accept(PeerAddr* peer) const
{
    struct sockaddr_storage addr{};
    socklen_t len = sizeof(addr);

#ifdef PLATFORM_WINDOWS
    socket_t client = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    if (client == INVALID_SOCKET) {
        return std::nullopt;
    }
#else
    // On Linux/macOS we can use accept4 if available to set SOCK_NONBLOCK atomically
#ifdef PLATFORM_LINUX
    socket_t client = ::accept4(fd_, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_NONBLOCK);
#else
    socket_t client = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
#endif
    if (client == kInvalidSocket) {
        if (would_block() || errno == EINTR)
            return std::nullopt;
        LOG_ERROR("socket", "accept() failed: {}", strerror(errno));
        return std::nullopt;
    }
#endif

    Socket s(client);

#ifndef PLATFORM_LINUX   // accept4 already set non-blocking on Linux
    s.set_nonblocking();
#endif
    s.set_nodelay();

    if (peer) {
        *peer = parse_address(addr);
    }

    return s;
}

std::expected<size_t, IoError>
Socket::read(std::span<uint8_t> buffer) const
{
#ifdef PLATFORM_WINDOWS
    int n = ::recv(fd_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (n > 0)
        return static_cast<size_t>(n);
    if (n == 0)
        return std::unexpected(IoError::Closed);
    return std::unexpected(would_block()
        ? IoError::WouldBlock
        : IoError::Fatal);
#else
    ssize_t n;
    do {
        n = ::recv(fd_, buffer.data(), buffer.size(), 0);
    } while (n < 0 && errno == EINTR);

    if (n > 0)
        return static_cast<size_t>(n);
    if (n == 0)
        return std::unexpected(IoError::Closed);
    return std::unexpected(would_block()
        ? IoError::WouldBlock
        : IoError::Fatal);
#endif
}

std::expected<size_t, IoError>
Socket::write(std::span<uint8_t const> buffer) const
{
#ifdef PLATFORM_WINDOWS
    int n = ::send(fd_, reinterpret_cast<char const*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (n == SOCKET_ERROR) {
        return std::unexpected(would_block()
            ? IoError::WouldBlock
            : IoError::Fatal);
    }
    return static_cast<size_t>(n);
#else
    ssize_t n;
    do {
        n = ::send(fd_, buffer.data(), buffer.size(), MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return std::unexpected(would_block()
            ? IoError::WouldBlock
            : IoError::Fatal);
    }
    return static_cast<size_t>(n);
#endif
}

void
Socket::set_nonblocking()
{
#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    ioctlsocket(fd_, FIONBIO, &mode);
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
}

void
Socket::set_nodelay()
{
    int on = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<char const*>(&on), sizeof(on));
}

void
Socket::set_reuseaddr()
{
    int on = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char const*>(&on), sizeof(on));
}

void
Socket::set_reuseport()
{
#ifdef SO_REUSEPORT
    int on = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT,
               reinterpret_cast<char const*>(&on), sizeof(on));
#endif
}

PeerAddr
Socket::peer_addr() const
{
    struct sockaddr_storage addr{};
    socklen_t len = sizeof(addr);

    if (getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return {};
    }

    return parse_address(addr);
}

PeerAddr
Socket::local_addr() const
{
    struct sockaddr_storage addr{};
    socklen_t len = sizeof(addr);

    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return {};
    }

    return parse_address(addr);
}

void
Socket::close_fd()
{
    if (fd_ == kInvalidSocket)
        return;

#ifdef PLATFORM_WINDOWS
    ::closesocket(fd_);
#else
    ::close(fd_);
#endif

    fd_ = kInvalidSocket;
}
