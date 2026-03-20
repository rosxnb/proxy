#include "proxy/ProxyConfig.hpp"
#include "utils/ArgumetParser.hpp"

#include <print>


static void
print_usage(std::string_view prog)
{
    std::println("Usage: {} [OPTIONS]\n", prog);
    std::println("Options:");
    std::println("  --listen-host HOST      Bind address          (default: 127.0.0.1)");
    std::println("  --listen-port PORT      Bind port             (default: 8080)");
    std::println("  --mode forward|trans    Proxy mode            (default: forward)");
    std::println("  --upstream-host HOST    Upstream host         (forward mode only)");
    std::println("  --upstream-port PORT    Upstream port         (forward mode only)");
    std::println("  --max-conn N            Max simultaneous connections (0=unlimited)");
    std::println("  --log-level (debug|info|warn|error)                  (default: info)");
    std::println("  --hex-dump              Hex-dump data in log         (default: off)");
    std::println("  --help                  Show this help\n");
    std::println("\nExamples:");
    std::println("  # Forward all connections to example.com:80");
    std::println("  {} --mode forward --upstream-host example.com --upstream-port 80\n", prog);
    std::println("  # Transparent proxy on port 8080 (requires OS NAT rule)");
    std::println("  {} --mode trans --listen-port 8080 --log-level debug", prog);
}


ProxyConfig
get_config(int argc, char** argv)
{
    ArgumentParser argparser(argc, argv);
    ProxyConfig    cfg;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];

        if (argparser.has("--help")) {
            print_usage(argparser.program_name());
            exit(0);
        }
        else if (auto val = argparser.get("--listen-host")) {
            cfg.listen_host = val.value();
        }
        else if (auto val = argparser.get("--listen-port")) {
            cfg.listen_port = static_cast<uint16_t>(std::stoi(val.value().data()));
        }
        else if (auto val = argparser.get("--mode")) {
            auto m = val.value();
            if (m == "forward")
                cfg.mode = ProxyMode::Forward;
            else if (m == "trans")
                cfg.mode = ProxyMode::Transparent;
            else {
                std::println(stderr, "Unknown mode: {}", m);
                exit(1);
            }
        }
        else if (auto val = argparser.get("--upstream-host")) {
            cfg.upstream_host = val.value();
        }
        else if (auto val = argparser.get("--upstream-port")) {
            cfg.upstream_port = static_cast<uint16_t>(std::stoi(val.value().data()));
        }
        else if (auto val = argparser.get("--max-conn")) {
            cfg.max_connections = static_cast<uint32_t>(std::stoi(val.value().data()));
        }
        else if (auto val = argparser.get("--log-level")) {
            auto l = val.value();
            if      (l == "debug") cfg.log_level = LogLevel::Debug;
            else if (l == "info")  cfg.log_level = LogLevel::Info;
            else if (l == "warn")  cfg.log_level = LogLevel::Warn;
            else if (l == "error") cfg.log_level = LogLevel::Error;
            else {
                std::println(stderr, "Unknown log level: {}", l);
                exit(1);
            }
        }
        else if (argparser.has("--hex-dump")) {
            cfg.log_hex_dump = true;
        }
        else {
            std::println(stderr, "Unknown option: {}", a);
            print_usage(argv[0]);
            exit(1);
        }
    }

    return cfg;
}
