#include "proxy/OriginalDst.hpp"
#include "core/Logger.hpp"

// ── macOS (pf DIOCNATLOOK) ────────────────────────────────────────────────────
#ifdef PLATFORM_MACOS

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

// ---------------------------------------------------------------------------
// net/pfvar.h was removed from Apple's public SDK in macOS 15+ (sw_vers
// productVersion >= 15 / 26).  The structs and ioctl below are stable kernel
// ABI taken directly from the XNU open-source release — they have not changed
// since macOS 10.7 and are safe to embed here.
// Source: https://github.com/apple-oss-distributions/xnu  (bsd/net/pfvar.h)
// ---------------------------------------------------------------------------
#ifndef DIOCNATLOOK

union pf_addr
{
    struct in_addr   v4;
    struct in6_addr  v6;
    uint8_t          addr8[16];
    uint16_t         addr16[8];
    uint32_t         addr32[4];
};

struct pfioc_natlook
{
    union pf_addr    saddr;   // source address
    union pf_addr    daddr;   // destination address
    union pf_addr    rsaddr;  // translated source address (reply)
    union pf_addr    rdaddr;  // translated destination destination (reply)
    uint16_t         sport;
    uint16_t         dport;
    uint16_t         rsport;
    uint16_t         rdport;
    sa_family_t      af;
    uint8_t          proto;
    uint8_t          direction;
};

#define PF_OUT      2
#define DIOCNATLOOK _IOWR('D', 23, struct pfioc_natlook)

#endif // DIOCNATLOOK

std::optional<OriginalDst>
get_original_dst(int client_fd)
{
    // Retrieve local (proxy) and remote (client) addresses of the accepted socket.
    struct sockaddr_storage local_addr{}, remote_addr{};
    socklen_t len = sizeof(local_addr);

    if (getsockname(client_fd, reinterpret_cast<sockaddr*>(&local_addr), &len) != 0) {
        LOG_ERROR("origdst", "getsockname failed: {}", strerror(errno));
        return std::nullopt;
    }
    len = sizeof(remote_addr);
    if (getpeername(client_fd, reinterpret_cast<sockaddr*>(&remote_addr), &len) != 0) {
        LOG_ERROR("origdst", "getpeername failed: {}", strerror(errno));
        return std::nullopt;
    }

    // Only IPv4 supported via DIOCNATLOOK (IPv6 uses DIOCNATLOOK6, similar).
    if (local_addr.ss_family != AF_INET) {
        LOG_WARN("origdst", "DIOCNATLOOK only supports IPv4 currently");
        return std::nullopt;
    }

    int pf_fd = open("/dev/pf", O_RDONLY);
    if (pf_fd < 0) {
        LOG_ERROR("origdst",
                  "Cannot open /dev/pf: {}  (need root + 'sudo pfctl -e')",
                  strerror(errno));
        return std::nullopt;
    }

    struct pfioc_natlook nl{};
    nl.proto     = IPPROTO_TCP;
    nl.af        = AF_INET;
    nl.direction = PF_OUT;

    auto* rem = reinterpret_cast<sockaddr_in*>(&remote_addr);
    auto* loc = reinterpret_cast<sockaddr_in*>(&local_addr);

    nl.saddr.v4 = rem->sin_addr;
    nl.sport    = rem->sin_port;
    nl.daddr.v4 = loc->sin_addr;
    nl.dport    = loc->sin_port;

    if (ioctl(pf_fd, DIOCNATLOOK, &nl) != 0) {
        // ENOENT means no NAT entry — connection wasn't redirected
        if (errno != ENOENT)
            LOG_WARN("origdst", "DIOCNATLOOK failed: {}", strerror(errno));
        close(pf_fd);
        return std::nullopt;
    }
    close(pf_fd);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &nl.rdaddr.v4, ip, sizeof(ip));

    return OriginalDst{ std::string(ip), ntohs(nl.rdport) };
}

// ── Linux (SO_ORIGINAL_DST via netfilter) ─────────────────────────────────────
#elif defined(PLATFORM_LINUX)

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/netfilter_ipv4.h>
#include <cerrno>
#include <cstring>

std::optional<OriginalDst>
get_original_dst(int client_fd)
{
    struct sockaddr_in orig{};
    socklen_t len = sizeof(orig);

    if (getsockopt(client_fd, SOL_IP, SO_ORIGINAL_DST,
                   reinterpret_cast<sockaddr*>(&orig), &len) != 0)
    {
        if (errno != ENOENT)
            LOG_WARN("origdst", "SO_ORIGINAL_DST failed: {}", strerror(errno));
        return std::nullopt;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &orig.sin_addr, ip, sizeof(ip));
    return OriginalDst{ std::string(ip), ntohs(orig.sin_port) };
}

// ── Windows (not supported) ───────────────────────────────────────────────────
#else

std::optional<OriginalDst>
get_original_dst(int /*client_fd*/)
{
    LOG_WARN("origdst", "Transparent mode not supported on this platform yet since it requires driver");
    return std::nullopt;
}

#endif
