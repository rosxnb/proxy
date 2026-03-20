#include "interceptor/InterceptorChain.hpp"

void
InterceptorChain::add(std::unique_ptr<Interceptor> interceptor)
{
    chain_.push_back(std::move(interceptor));
}

bool
InterceptorChain::on_connect(ConnectionContext& ctx)
{
    for (auto& i : chain_)
        if (!i->on_connect(ctx))
            return false;
    return true;
}

bool
InterceptorChain::on_client_data(ConnectionContext& ctx, Buffer& data)
{
    for (auto& i : chain_)
        if (!i->on_client_data(ctx, data))
            return false;
    return true;
}

bool
InterceptorChain::on_server_data(ConnectionContext& ctx, Buffer& data)
{
    for (int i = static_cast<int>(chain_.size()) - 1; i >= 0; --i)
        if (!chain_[i]->on_server_data(ctx, data))
            return false;
    return true;
}

void
InterceptorChain::on_close(ConnectionContext& ctx)
{
    for (int i = static_cast<int>(chain_.size()) - 1; i >= 0; --i)
        chain_[i]->on_close(ctx);
}
