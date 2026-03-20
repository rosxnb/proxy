# TCP Proxy

A fully cross-platform, single-threaded, event-driven transparent TCP proxy
written in modern C++23.

## Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                          ProxyServer                              │
│  Listens · Accepts · Creates TcpTunnels · Manages lifetime        │
└───────────────────────────┬───────────────────────────────────────┘
                            │ creates
          ┌─────────────────▼────────────────────────────┐
          │             TcpTunnel                        │
          │  client ◄──── InterceptorChain ────►upstream │
          └──────────────────────────────────────────────┘
                            │ uses
          ┌─────────────────▼───────────────────┐
          │           EventLoop (abstract)      │
          │  KqueueEventLoop  ← macOS           │
          │  EpollEventLoop   ← Linux           │
          │  SelectEventLoop  ← Windows         │
          └─────────────────────────────────────┘
```

### Key extensibility points (for MITM later)

| Hook                            | Purpose                                    |
|---------------------------------|--------------------------------------------|
| `Interceptor::on_connect`       | Block/allow connections, inject context    |
| `Interceptor::on_client_data`   | Inspect/modify C→S bytes (e.g. strip TLS)  |
| `Interceptor::on_server_data`   | Inspect/modify S→C bytes (e.g. re-encrypt) |
| `Interceptor::on_close`         | Final stats, cleanup                       |
| `OriginalDst::get_original_dst` | Platform NAT lookup (pf/netfilter)         |
| `ProxyConfig::mode`             | Switch between Forward and Transparent     |

## Building

### Prerequisites
- CMake ≥ 4.2
- C++23 compiler (Clang on macOS, GCC/Clang on Linux, MSVC on Windows)


## Running

### Mode 1: Forward proxy (no root required, great for testing)

Forwards all connections to a single upstream host.

```bash
./tcp_proxy \
  --mode forward \
  --listen-port 8080 \
  --upstream-host httpbin.org \
  --upstream-port 80 \
  --log-level debug
```

Test it:
```bash
curl --proxy http://127.0.0.1:8080 http://httpbin.org/get
# or connect directly — in forward mode the proxy ignores original dst
nc 127.0.0.1 8080   # then type an HTTP request manually
```

### Mode 2: Transparent proxy (macOS — requires root)

Intercepts traffic redirected by pf NAT rules.

**Step 1** — Set up pf redirect (one-time):
```bash
sudo ./setup_macos_pf.sh enable 8080 80
```

**Step 2** — Start the proxy:
```bash
./tcp_proxy \
  --mode trans \
  --listen-port 8080 \
  --log-level debug
```

**Step 3** — Test (traffic is automatically intercepted):
```bash
curl http://httpbin.org/get    # proxy intercepts transparently
```

**Step 4** — Clean up:
```bash
sudo ./setup_macos_pf.sh disable
```

### Linux transparent mode

```bash
# Redirect port 80 → 8080 for loopback traffic
sudo iptables -t nat -A OUTPUT -p tcp --dport 80 -j REDIRECT --to-port 8080

./tcp_proxy --mode trans --listen-port 8080 --log-level debug

# Test
curl http://httpbin.org/get

# Cleanup
sudo iptables -t nat -D OUTPUT -p tcp --dport 80 -j REDIRECT --to-port 8080
```

## Writing an Interceptor

```cpp
#include "interceptor/Interceptor.hpp"

class MyInterceptor final : public Interceptor
{
public:
    bool on_connect(ConnectionContext& ctx) override
    {
        // Return false to block the connection entirely
        if (ctx.upstream_port == 22)
            return false; // block SSH
        return true;
    }

    bool on_client_data(ConnectionContext& ctx, Buffer& data) override
    {
        // Inspect or modify client→server bytes
        // data.clear();             // drop all data
        // data.append("hello", 5); // inject bytes
        return true;
    }

    bool on_server_data(ConnectionContext& ctx, Buffer& data) override
    {
        // Inspect or modify server→client bytes
        return true;
    }

    void on_close(ConnectionContext& ctx) override
    {
        // Log, cleanup, etc.
    }
};

// Register it through ProxyServer object
server.add_interceptor(std::make_unique<MyInterceptor>());
```

## Roadmap to full MITM

| Step      | Description                                         |
|-----------|-----------------------------------------------------|
| ✅ Step 1 | Transparent TCP relay (this codebase)               |
| 🔜 Step 2 | TLS termination via BoringSSL / OpenSSL interceptor |
| 🔜 Step 3 | HTTP/1.1 and HTTP/2 protocol parsers                |
| 🔜 Step 4 | Certificate authority + dynamic cert generation     |
| 🔜 Step 5 | Web UI / REST API for inspecting flows              |

## File structure

```
tcp_proxy/
├── CMakeLists.txt
├── setup_macos_pf.sh       ← pf NAT rules helper
└── src/
    ├── main.cpp
    ├── core/
    │   ├── Buffer.hpp       ← Sliding window byte buffer
    │   ├── Logger.hpp       ← Thread-safe coloured logger
    │   ├── EventLoop.hpp    ← Abstract event loop interface
    │   ├── Socket.hpp/.cpp  ← RAII cross-platform socket wrapper
    ├── platform/
    │   ├── KqueueEventLoop  ← macOS / BSD
    │   ├── EpollEventLoop   ← Linux
    │   └── SelectEventLoop  ← Windows / fallback
    ├── interceptor/
    │   ├── Interceptor.hpp       ← Base hook interface + ConnectionContext
    │   ├── InterceptorChain      ← Ordered middleware stack
    │   └── LoggingInterceptor    ← Sample: log all connections & byte counts
    └── proxy/
        ├── ProxyConfig.hpp  ← Flat config struct
        ├── OriginalDst      ← NAT lookup (pf/netfilter)
        ├── TcpTunnel        ← Bidirectional async relay
        └── ProxyServer      ← Accept loop + tunnel lifecycle
```
