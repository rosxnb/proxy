#pragma once

#include <cstdint>
#include <string>

#include "core/Logger.hpp"


// ---------------------------------------------------------------------------
// ProxyMode — determines how the proxy resolves the upstream destination.
// ---------------------------------------------------------------------------
enum class ProxyMode
{
    /// Forward mode: all traffic is forwarded to a single configured host:port.
    /// Great for development and testing without OS-level traffic redirection.
    Forward,

    /// Transparent mode: the original destination is recovered from the OS
    /// NAT state (pf on macOS, iptables on Linux).
    /// Requires root privileges and OS firewall rules to redirect traffic.
    Transparent,
};

struct ProxyConfig
{
    ProxyMode mode = ProxyMode::Forward;

    std::string listen_host = "127.0.0.1";
    uint16_t    listen_port = 8080;

    uint32_t max_connections = 0;           // Max simultaneous connections (0 = unlimited)
    size_t   read_buffer_size = 64 * 1024;  // Per-connection read buffer size
    bool     log_hex_dump = false;          // Enable verbose hex dump in LoggingInterceptor
    LogLevel log_level = LogLevel::Debug;

    // ── Forward mode only ────────────────────────────────────────────────────
    std::string upstream_host = "example.com";
    uint16_t    upstream_port = 80;
};


ProxyConfig get_config(int argc, char** argv);
