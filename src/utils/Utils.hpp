#pragma once

#include <chrono>
#include <string>
#include <array>


inline std::tm
get_local_tm()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};

#ifdef PLATFORM_WINDOWS
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif
    return local_tm;
}

inline std::string
to_string(std::tm const& tm, std::string_view format = "%Y-%m-%dT%H:%M:%SZ")
{
    std::array<char, 128> buffer;

    size_t rv = std::strftime(buffer.data(), buffer.size(), format.data(), &tm);
    return rv
        ? std::string(buffer.data())
        : std::string{};
}
