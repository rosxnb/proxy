#pragma once

#include "core/Buffer.hpp"

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>

struct ConnectionContext
{
    uint64_t    id          = 0;
    std::string client_ip;
    uint16_t    client_port = 0;
    std::string target_ip;
    uint16_t    target_port = 0;
    std::string upstream_host;
    uint16_t    upstream_port = 0;
    uint64_t    bytes_client_to_server = 0;
    uint64_t    bytes_server_to_client = 0;

    struct MetaBag
    {
        template<class T>
        void set(std::string const& k, T v)
        {
            data_[k] = std::move(v);
        }

        template<class T>
        T get(std::string const& k, T def = T{}) const
        {
            auto it = data_.find(k);
            if (it == data_.end())
                return def;
            return std::any_cast<T>(it->second);
        }

        bool has(std::string const& k) const
        {
            return data_.contains(k);
        }

    private:
        std::unordered_map<std::string, std::any> data_;
    };

    MetaBag meta;
};

class Interceptor
{
public:
    virtual ~Interceptor() = default;

    // Return false from any hook to close the connection immediately.

    /// Both sockets are connected and ready.
    virtual bool on_connect(ConnectionContext& /*ctx*/)
    { return true; }

    /// Data flowing CLIENT → SERVER. Modify buffer in-place to alter bytes forwarded.
    virtual bool on_client_data(ConnectionContext& /*ctx*/, Buffer& /*data*/)
    { return true; }

    /// Data flowing SERVER → CLIENT. Modify buffer in-place to alter bytes forwarded.
    virtual bool on_server_data(ConnectionContext& /*ctx*/, Buffer& /*data*/)
    { return true; }

    /// Connection is about to be torn down.
    virtual void on_close(ConnectionContext& /*ctx*/)
    {}
};
