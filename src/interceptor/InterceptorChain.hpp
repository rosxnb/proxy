#pragma once

#include "interceptor/Interceptor.hpp"
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// InterceptorChain — an ordered collection of Interceptors.
//
// Execution order:
//   on_connect / on_client_data: interceptors[0] → [1] → … → [n-1]
//   on_server_data:              interceptors[n-1] → … → [1] → [0]
//   on_close:                    interceptors[n-1] → … → [0] (reverse)
//
// This "onion" model means the first-added interceptor is outermost —
// useful for e.g. adding a logging interceptor before a TLS interceptor.
// ---------------------------------------------------------------------------

class InterceptorChain
{
public:
    void add(std::unique_ptr<Interceptor> interceptor);

    bool on_connect(ConnectionContext& ctx);
    bool on_client_data(ConnectionContext& ctx, Buffer& data);
    bool on_server_data(ConnectionContext& ctx, Buffer& data);
    void on_close(ConnectionContext& ctx);

    bool empty() const { return chain_.empty(); }

private:
    std::vector<std::unique_ptr<Interceptor>> chain_;
};
