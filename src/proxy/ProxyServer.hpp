#pragma once

#include "proxy/ProxyConfig.hpp"
#include "proxy/TcpTunnel.hpp"
#include "interceptor/InterceptorChain.hpp"
#include "core/EventLoop.hpp"
#include "core/Socket.hpp"

#include <memory>
#include <unordered_map>
#include <atomic>
#include <csignal>

// ---------------------------------------------------------------------------
// ProxyServer — top-level object.
//
// Usage:
//   ProxyConfig cfg;
//   cfg.mode         = ProxyMode::Transparent;
//   cfg.listen_port  = 8080;
//
//   ProxyServer server(cfg);
//   server.add_interceptor(std::make_unique<LoggingInterceptor>());
//   server.run();   // blocks until SIGINT / SIGTERM or server.stop()
// ---------------------------------------------------------------------------

class ProxyServer
{
public:
    explicit ProxyServer(ProxyConfig config);

    /// Add an interceptor BEFORE calling run().
    void add_interceptor(std::unique_ptr<Interceptor> interceptor);

    /// Start the event loop.  Blocks until stop() is called.
    void run();

    /// Signal the server to shut down cleanly.
    void stop();

    uint64_t total_connections()   const { return total_connections_; }
    size_t   active_connections()  const { return tunnels_.size(); }

private:
    void on_new_connection();
    void remove_tunnel(uint64_t id);

    ProxyConfig      config_;
    InterceptorChain chain_;

    std::unique_ptr<EventLoop>                               loop_;
    Socket                                                   listener_;
    std::unordered_map<uint64_t, std::shared_ptr<TcpTunnel>> tunnels_;

    std::atomic<uint64_t>  next_id_{1};
    uint64_t               total_connections_ = 0;
};
