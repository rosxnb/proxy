#pragma once

#include "core/Socket.hpp"
#include "core/Buffer.hpp"
#include "core/EventLoop.hpp"
#include "interceptor/Interceptor.hpp"
#include "interceptor/InterceptorChain.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

/**
 * @class TcpTunnel
 * @brief Manages a single bi-directional proxied connection between a client and an upstream server.
 *
 * @details 
 * Lifecycle of a TcpTunnel:
 * 1. ProxyServer accepts a client socket, wraps it in a std::shared_ptr<TcpTunnel>, and calls start().
 * 2. TcpTunnel initiates a non-blocking connect() to the upstream server.
 * 3. Once upstream connects (write event fires), `on_connect` interceptors run.
 * 4. Both sockets are watched for readability. The async relay loop runs:
 * - Client readable -> read into scratch -> `on_client_data` interceptors -> buffer -> write upstream.
 * - Upstream readable -> read into scratch -> `on_server_data` interceptors -> buffer -> write client.
 * 5. If either side closes or encounters a fatal IoError, close() is triggered, running `on_close` interceptors.
 * 6. TcpTunnel invokes the `close_cb` so the ProxyServer can erase it from its active connections map.
 *
 * All I/O is strictly non-blocking. Writes that return `IoError::WouldBlock` are buffered 
 * in `write_buf_client_` / `write_buf_upstream_` and flushed automatically when write events fire.
 *
 * @warning Must be heap-allocated and managed via std::shared_ptr to safely bind callbacks to the EventLoop.
 */
class TcpTunnel : public std::enable_shared_from_this<TcpTunnel>
{
public:
    /**
     * @brief Callback invoked when the tunnel completely closes.
     * @param id The unique identifier of the connection context.
     */
    using CloseCb = std::function<void(uint64_t id)>;

    /**
     * @brief Constructs a new TcpTunnel.
     * @param id Unique identifier for this tunnel.
     * @param client_socket The already-connected client Socket (unique ownership transferred).
     * @param ctx The connection context (metadata about the connection).
     * @param loop Reference to the main EventLoop driving this proxy.
     * @param interceptors Reference to the chain of traffic interceptors.
     * @param close_cb Callback to notify the parent ProxyServer upon termination.
     * @param read_buf_size Size of the pre-allocated read scratchpad (defaults to 64KB).
     */
    TcpTunnel(uint64_t            id,
              Socket              client_socket,
              ConnectionContext   ctx,
              EventLoop&          loop,
              InterceptorChain&   interceptors,
              CloseCb             close_cb,
              size_t              read_buf_size = 64 * 1024);

    ~TcpTunnel();

    /**
     * @brief Initiates the upstream connection and registers initial event loop watches.
     * @note This must be called immediately after constructing the shared_ptr instance.
     */
    void start();

    /**
     * @brief Retrieves the unique connection ID.
     * @return The uint64_t ID associated with this tunnel's context.
     */
    [[nodiscard]] uint64_t id() const { return ctx_.id; }

private:
    /**
     * @brief Callback fired by the EventLoop when the non-blocking upstream connect() completes.
     */
    void on_upstream_connected();

    /**
     * @brief Callback fired when the client socket has data available to read.
     */
    void on_client_readable();

    /**
     * @brief Callback fired when the upstream socket has data available to read.
     */
    void on_upstream_readable();

    /**
     * @brief Attempts to flush the upstream write buffer. Fired on socket writability.
     */
    void flush_to_upstream();

    /**
     * @brief Attempts to flush the client write buffer. Fired on socket writability.
     */
    void flush_to_client();

    /**
     * @brief Tears down the tunnel, unwatches sockets, and triggers the close callback.
     * @param reason A human-readable reason for the closure (useful for debugging/logging).
     */
    void close(char const* reason = "");

    /**
     * @brief Attempts to drain the provided buffer into the target socket using C++23 std::expected.
     * @details If the socket returns `IoError::WouldBlock`, the remaining data stays in the buffer 
     * and this method registers a write watch with the EventLoop.
     * @param sock The destination Socket.
     * @param buf The Buffer containing the data to send.
     * @return true if the write succeeded (or cleanly blocked). false if a fatal IoError occurred.
     */
    bool try_write(Socket& sock, Buffer& buf);

private:
    bool closed_ = false; ///< Prevents re-entrant closures if multiple errors fire simultaneously.

    uint64_t          id_;
    Socket            client_;
    Socket            upstream_;
    ConnectionContext ctx_;
    EventLoop&        loop_;
    InterceptorChain& interceptors_;
    CloseCb           close_cb_;
    size_t            read_buf_size_;

    // Pending outbound data (bytes waiting to be written)
    Buffer write_buf_upstream_;   ///< Data read from client, waiting to be written to upstream
    Buffer write_buf_client_;     ///< Data read from upstream, waiting to be written to client

    // Scratch read buffer (reused each read call to avoid allocations)
    std::vector<uint8_t> read_scratch_; 
};
