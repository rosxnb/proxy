#include "proxy/ProxyServer.hpp"
#include "proxy/ProxyConfig.hpp"
#include "interceptor/LoggingInterceptor.hpp"
#include "core/Logger.hpp"

#include <cstdlib>
#include <cstring>


int main(int argc, char** argv)
{
    ProxyConfig cfg = get_config(argc, argv);

    Logger::instance().set_level(cfg.log_level);

    try {
        ProxyServer server(cfg);
        server.add_interceptor(std::make_unique<LoggingInterceptor>(cfg.log_hex_dump));
        server.run();
    } catch (std::exception const& ex) {
        LOG_ERROR("main", "Fatal: {}", ex.what());
        return 1;
    } catch (...) {
        LOG_ERROR("main", "Fatal: encountered an unknown exception");
        return 1;
    }

    return 0;
}
