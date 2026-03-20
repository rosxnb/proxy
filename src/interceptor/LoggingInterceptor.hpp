#pragma once

#include "interceptor/Interceptor.hpp"
#include "core/Logger.hpp"

// ---------------------------------------------------------------------------
// LoggingInterceptor — a sample Interceptor that:
//   - Logs connection open / close with client and target addresses.
//   - Logs byte counts for each data chunk in both directions.
//   - Optionally hex-dumps payload bytes (enable with dump_hex = true).
//
// This is the canonical example of how to write an Interceptor.
// For MITM TLS inspection, you would add a TlsInterceptor alongside this.
// ---------------------------------------------------------------------------

class LoggingInterceptor final : public Interceptor
{
public:
    explicit LoggingInterceptor(bool dump_hex = false)
        : dump_hex_(dump_hex)
    {}

    bool on_connect(ConnectionContext& ctx) override
    {
        LOG_INFO("log", "[#{}] CONNECT {}:{}  →  {}:{}",
                 ctx.id, ctx.client_ip.c_str(), ctx.client_port,
                 ctx.upstream_host.c_str(), ctx.upstream_port);
        return true;
    }

    bool on_client_data(ConnectionContext& ctx, Buffer& data) override
    {
        LOG_DEBUG("log", "[#{}] C→S {} bytes", ctx.id, data.size());
        if (dump_hex_ && data.size() > 0)
            hex_dump("C→S", data);
        return true;
    }

    bool on_server_data(ConnectionContext& ctx, Buffer& data) override
    {
        LOG_DEBUG("log", "[#{}] S→C {} bytes", ctx.id, data.size());
        if (dump_hex_ && data.size() > 0)
            hex_dump("S→C", data);
        return true;
    }

    void on_close(ConnectionContext& ctx) override
    {
        LOG_INFO("log",
                 "[#{}] CLOSE  C→S={} bytes  S→C={} bytes",
                 ctx.id,
                 ctx.bytes_client_to_server,
                 ctx.bytes_server_to_client);
    }

private:
    bool dump_hex_;

    static void hex_dump(char const* label, Buffer const& buf)
    {
        uint8_t const* p   = buf.data();
        size_t         len = std::min(buf.size(), size_t(256));
        char           line[80];

        for (size_t off = 0; off < len; off += 16) {
            int n = 0;

            n += snprintf(line + n, sizeof(line) - n, "  %04zx  ", off);
            for (size_t j = 0; j < 16; j++) {
                if (off + j < len)
                    n += snprintf(line+n, sizeof(line)-n, "%02x ", p[off+j]);
                else
                    n += snprintf(line+n, sizeof(line)-n, "   ");
            }

            n += snprintf(line + n, sizeof(line) - n, " |");
            for (size_t j = 0; j < 16 && off+j < len; j++) {
                char c = static_cast<char>(p[off+j]);
                n += snprintf(line+n, sizeof(line)-n, "%c",
                              (c >= 0x20 && c < 0x7f) ? c : '.');
            }

            snprintf(line + n, sizeof(line) - n, "|");
            LOG_DEBUG(label, "{}", line);
        }
    }
};
