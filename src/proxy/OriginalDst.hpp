#pragma once

#include <cstdint>
#include <string>
#include <optional>

// ---------------------------------------------------------------------------
// OriginalDst — recovers the original destination address of a redirected
// TCP connection (transparent proxy mode).
//
// macOS:
//   Uses pf's DIOCNATLOOK ioctl on /dev/pf.
//   Requires: sudo pfctl -e and a rdr-to rule active.
//
// Linux:
//   Uses getsockopt(SO_ORIGINAL_DST) from netfilter.
//   Requires: iptables -t nat -A PREROUTING -p tcp --dport X -j REDIRECT
//
// Windows:
//   Not supported; transparent mode will not be available.
// ---------------------------------------------------------------------------

struct OriginalDst
{
    std::string ip;
    uint16_t    port = 0;
};

/// Given an already-accepted client socket (file descriptor), retrieve the
/// original destination IP and port.
/// Returns nullopt on failure (e.g., no NAT entry found, insufficient
/// privileges, or platform not supported).
std::optional<OriginalDst> get_original_dst(int client_fd);
