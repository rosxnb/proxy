#include "proxy/TcpTunnel.hpp"
#include "core/Logger.hpp"

#include <cassert>
#include <cstring>
#include <span>

#ifndef PLATFORM_WINDOWS
#  include <sys/socket.h>
#  include <cerrno>
#endif

// ── Constructor / Destructor ──────────────────────────────────────────────────

TcpTunnel::TcpTunnel(uint64_t            id,
                     Socket              client_socket,
                     ConnectionContext   ctx,
                     EventLoop&          loop,
                     InterceptorChain&   interceptors,
                     CloseCb             close_cb,
                     size_t              read_buf_size)
    : id_(id)
    , client_(std::move(client_socket))
    , ctx_(std::move(ctx))
    , loop_(loop)
    , interceptors_(interceptors)
    , close_cb_(std::move(close_cb))
    , read_buf_size_(read_buf_size)
{
    read_scratch_.resize(read_buf_size_);
}

TcpTunnel::~TcpTunnel()
{
    // Unwatch sockets that might still be open (safety net — close() should
    // already have been called, but we guard against double-free).

    if (client_.valid())
        loop_.unwatch(client_.fd());

    if (upstream_.valid())
        loop_.unwatch(upstream_.fd());
}

// ── Public entry point ────────────────────────────────────────────────────────

void
TcpTunnel::start()
{
    auto self = shared_from_this();

    // Initiate non-blocking connect to upstream.
    upstream_ = [&]() -> Socket {
        auto s = Socket::make_connector(ctx_.upstream_host, ctx_.upstream_port);
        if (!s)
            return Socket{};
        return std::move(*s);
    }();

    if (!upstream_.valid()) {
        LOG_WARN("tunnel", "[#{}] Could not create upstream socket to {}:{}",
                 (unsigned long long)id_,
                 ctx_.upstream_host.c_str(), ctx_.upstream_port);
        close("upstream socket creation failed");
        return;
    }

    // Watch upstream for writability — signals connect() completed.
    loop_.watch_write(upstream_.fd(), [self]() {
        self->on_upstream_connected();
    });
}

// ── Phase 2: upstream connected ───────────────────────────────────────────────

void
TcpTunnel::on_upstream_connected()
{
    if (closed_)
        return;

    // Confirm the connect() actually succeeded (non-blocking connect can
    // report write-ready even on error — check SO_ERROR).
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(upstream_.fd(), SOL_SOCKET, SO_ERROR,
               reinterpret_cast<char*>(&err), &len);

    if (err != 0) {
        LOG_WARN("tunnel", "[#{}] Upstream connect() failed: {}",
                 (unsigned long long)id_, strerror(err));
        close("upstream connect failed");
        return;
    }

    // Stop watching for writability (connect check done).
    loop_.unwatch_write(upstream_.fd());

    ctx_.upstream_port = [&]() -> uint16_t {
        auto pa = upstream_.peer_addr();
        ctx_.upstream_host = pa.ip;
        return pa.port;
    }();

    LOG_INFO("tunnel", "[#{}] Connected  {}:{}  →  {}:{}",
             (unsigned long long)id_,
             ctx_.client_ip.c_str(),   ctx_.client_port,
             ctx_.upstream_host.c_str(), ctx_.upstream_port);

    // Run on_connect interceptors.
    if (!interceptors_.on_connect(ctx_)) {
        close("interceptor blocked connection");
        return;
    }

    // Register bidirectional relay.
    auto self = shared_from_this();
    loop_.watch_read(client_.fd(),   [self]() { self->on_client_readable();   });
    loop_.watch_read(upstream_.fd(), [self]() { self->on_upstream_readable(); });
}

// ── Phase 3: relay ────────────────────────────────────────────────────────────

void
TcpTunnel::on_client_readable()
{
    if (closed_)
        return;

    std::span<uint8_t> scratch_span{read_scratch_.data(), read_scratch_.size()};
    auto result = client_.read(scratch_span);

    if (!result) {
        switch (result.error()) {
            case IoError::Closed:
                LOG_DEBUG("tunnel", "[#{}] Client closed connection (EOF)", (unsigned long long)id_);
                close("client EOF");
                return;
            case IoError::Fatal:
                close("client read error");
                return;
            case IoError::WouldBlock:
                return; // Nothing to do yet, wait for next event loop tick
        }
    }

    size_t bytes_read = result.value();

    // Wrap raw bytes in a Buffer for interceptor processing.
    Buffer data;
    data.append(std::span<uint8_t const>{read_scratch_.data(), bytes_read});
    ctx_.bytes_client_to_server += bytes_read;

    if (!interceptors_.on_client_data(ctx_, data)) {
        close("interceptor dropped client data");
        return;
    }

    if (data.empty())
        return; // interceptor swallowed the data

    write_buf_upstream_.append(data);
    flush_to_upstream();
}

void
TcpTunnel::on_upstream_readable()
{
    if (closed_)
        return;

    std::span<uint8_t> scratch_span{read_scratch_.data(), read_scratch_.size()};
    auto result = upstream_.read(scratch_span);

    if (!result) {
        switch (result.error()) {
            case IoError::Closed:
                LOG_DEBUG("tunnel", "[#{}] Upstream closed connection (EOF)", (unsigned long long)id_);
                close("upstream EOF");
                return;
            case IoError::Fatal:
                close("upstream read error");
                return;
            case IoError::WouldBlock:
                return; // EAGAIN
        }
    }

    size_t bytes_read = result.value();

    Buffer data;
    data.append(std::span<uint8_t const>{read_scratch_.data(), bytes_read});
    ctx_.bytes_server_to_client += bytes_read;

    if (!interceptors_.on_server_data(ctx_, data)) {
        close("interceptor dropped server data");
        return;
    }

    if (data.empty())
        return;

    write_buf_client_.append(data);
    flush_to_client();
}

// ── Flush helpers ─────────────────────────────────────────────────────────────

void
TcpTunnel::flush_to_upstream()
{
    if (!try_write(upstream_, write_buf_upstream_)) {
        close("upstream write error");
        return;
    }

    auto self = shared_from_this();
    if (!write_buf_upstream_.empty()) {
        // Couldn't write everything — wait for writability.
        loop_.watch_write(upstream_.fd(), [self]() {
            if (self->closed_)
                return;

            self->loop_.unwatch_write(self->upstream_.fd());
            self->flush_to_upstream();
        });
    }
}

void
TcpTunnel::flush_to_client()
{
    if (!try_write(client_, write_buf_client_)) {
        close("client write error");
        return;
    }

    auto self = shared_from_this();
    if (!write_buf_client_.empty()) {
        loop_.watch_write(client_.fd(), [self]() {
            if (self->closed_)
                return;
            self->loop_.unwatch_write(self->client_.fd());
            self->flush_to_client();
        });
    }
}

bool
TcpTunnel::try_write(Socket& sock, Buffer& buf)
{
    while (!buf.empty()) {
        // Cast the buffer's data pointer to a const uint8_t span for the modern Socket API
        std::span<uint8_t const> out_span{reinterpret_cast<uint8_t const*>(buf.data()), buf.size()};
        
        auto result = sock.write(out_span);
        
        if (result) {
            buf.advance(result.value());
        } else {
            // EAGAIN: socket buffer full — caller must register write watch
            if (result.error() == IoError::WouldBlock) {
                return true; 
            }
            // Fatal error or unexpected EOF during write
            return false;
        }
    }
    return true;
}

// ── Teardown ──────────────────────────────────────────────────────────────────

void
TcpTunnel::close(char const* reason)
{
    if (closed_)
        return;
    closed_ = true;

    LOG_DEBUG("tunnel", "[#{}] Closing: {}", (unsigned long long)id_, reason);

    interceptors_.on_close(ctx_);

    if (client_.valid())
        loop_.unwatch(client_.fd());
    if (upstream_.valid())
        loop_.unwatch(upstream_.fd());

    // Sockets are closed by their destructors when they go out of scope,
    // but we explicitly invalidate them now to prevent double-close.
    client_   = Socket{};
    upstream_ = Socket{};

    // Schedule removal from the ProxyServer map (deferred to avoid
    // destroying ourselves while we're still on the call stack).
    if (close_cb_) {
        auto cb = std::move(close_cb_);
        auto id = id_;
        loop_.post([cb, id]() { cb(id); });
    }
}
