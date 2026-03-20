#include "proxy/ProxyServer.hpp"
#include "proxy/OriginalDst.hpp"
#include "core/Logger.hpp"

#include <csignal>
#include <stdexcept>

// ── Constructor ───────────────────────────────────────────────────────────────

ProxyServer::ProxyServer(ProxyConfig config)
    : config_(std::move(config))
    , loop_(EventLoop::create())
{}

void
ProxyServer::add_interceptor(std::unique_ptr<Interceptor> interceptor)
{
    chain_.add(std::move(interceptor));
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void
ProxyServer::run()
{
    // ── Create listening socket ──────────────────────────────────────────────
    auto maybe_listener = Socket::make_listener(config_.listen_host,
                                                config_.listen_port);
    if (!maybe_listener)
        throw std::runtime_error("Failed to bind listener socket");

    listener_ = std::move(*maybe_listener);

    LOG_INFO("server", "Proxy listening on {}:{}  mode={}",
             config_.listen_host.c_str(), config_.listen_port,
             config_.mode == ProxyMode::Transparent ? "transparent" : "forward");

    if (config_.mode == ProxyMode::Transparent) {
#ifdef PLATFORM_MACOS
        LOG_INFO("server",
                 "macOS transparent mode: ensure pf is active and rdr-to rule is set.\n"
                 "  Example /etc/pf.conf snippet:\n"
                 "    rdr pass on lo0 proto tcp from any to any port 80 -> 127.0.0.1 port {}\n"
                 "  Then: sudo pfctl -ef /etc/pf.conf",
                 config_.listen_port);
#elif defined(PLATFORM_LINUX)
        LOG_INFO("server",
                 "Linux transparent mode: ensure iptables rule is set.\n"
                 "  Example:\n"
                 "    sudo iptables -t nat -A OUTPUT -p tcp --dport 80 \\\n"
                 "        -j REDIRECT --to-port {}",
                 config_.listen_port);
#endif
    }

    // ── Register accept handler ──────────────────────────────────────────────
    loop_->watch_read(listener_.fd(), [this]() {
        on_new_connection();
    });

    // ── SIGINT / SIGTERM → graceful shutdown ─────────────────────────────────
    // We store a raw pointer for the signal handler (signal handlers can't
    // capture lambdas, so we use a global pointer).
    static ProxyServer* g_server = nullptr;
    g_server = this;

    auto sig_handler = [](int) {
        if (g_server)
            g_server->stop();
    };
    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    loop_->run();

    LOG_INFO("server", "Proxy stopped. total_connections={}",
             (unsigned long long)total_connections_);
}

void
ProxyServer::stop()
{
    LOG_INFO("server", "Shutdown requested");
    loop_->stop();
}

// ── Accept handler ────────────────────────────────────────────────────────────

void
ProxyServer::on_new_connection()
{
    // Drain all pending accepts in this single event (level-triggered
    // event loops will re-fire, but it's more efficient to batch here).
    while (true) {
        PeerAddr peer;
        auto maybe_sock = listener_.accept(&peer);
        if (!maybe_sock)
            break;   // EAGAIN — no more pending connections

        // ── Check connection limits ──────────────────────────────────────
        if (config_.max_connections > 0 &&
            tunnels_.size() >= config_.max_connections) {
            LOG_WARN("server", "Connection limit ({}) reached; dropping {}:{}",
                     config_.max_connections,
                     peer.ip.c_str(), peer.port);
            continue; // Socket destructor closes it immediately
        }

        // ── Resolve upstream destination ─────────────────────────────────
        ConnectionContext ctx;
        ctx.id          = next_id_++;
        ctx.client_ip   = peer.ip;
        ctx.client_port = peer.port;

        if (config_.mode == ProxyMode::Transparent) {
            auto orig = get_original_dst(maybe_sock->fd());
            if (!orig) {
                LOG_WARN("server",
                         "[#{}] Could not resolve original destination "
                         "(is the NAT rule active?); dropping connection from {}:{}",
                         (unsigned long long)ctx.id,
                         peer.ip.c_str(), peer.port);
                continue;
            }
            ctx.upstream_host = orig->ip;
            ctx.upstream_port = orig->port;
        } else {
            // Forward mode: always connect to the configured upstream
            ctx.upstream_host = config_.upstream_host;
            ctx.upstream_port = config_.upstream_port;
        }

        LOG_DEBUG("server",
                  "[#{}] Accepted from {}:{}  →  {}:{}",
                  (unsigned long long)ctx.id,
                  peer.ip.c_str(), peer.port,
                  ctx.upstream_host.c_str(), ctx.upstream_port);

        // ── Build and start tunnel ───────────────────────────────────────
        uint64_t id = ctx.id;
        auto tunnel = std::make_shared<TcpTunnel>(
            id,
            std::move(*maybe_sock),
            std::move(ctx),
            *loop_,
            chain_,
            [this](uint64_t closed_id) { remove_tunnel(closed_id); },
            config_.read_buffer_size
        );

        tunnels_.emplace(id, tunnel);
        total_connections_++;
        tunnel->start();
    }
}

// ── Tunnel removal ────────────────────────────────────────────────────────────

void
ProxyServer::remove_tunnel(uint64_t id)
{
    tunnels_.erase(id);
    LOG_DEBUG("server", "Tunnel #{} removed. active={}",
              (unsigned long long)id, tunnels_.size());
}
