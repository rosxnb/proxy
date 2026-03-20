#pragma once

#include <format>
#include <print>
#include <mutex>
#include <string_view>

#include <utils/Utils.hpp>


// Minimal Windows includes for terminal colors
#ifdef PLATFORM_WINDOWS
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#endif


enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

class Logger
{
public:
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    void set_level(LogLevel level)
    { level_ = level; }

    template <typename... Args>
    void log(LogLevel lvl, std::string_view tag, std::format_string<Args...> fmt, Args&&... args)
    {
        if (lvl < level_)
            return;

        std::string local_time = to_string(get_local_tm());
        auto msg = std::format(fmt, std::forward<Args>(args)...);

        std::lock_guard lock(mu_);
        std::println(stderr, "{} {}[{}]\033[0m [{}] {}",
                     local_time, colour_for(lvl), label_for(lvl), tag, msg);
    }

private:
    Logger()
    {
        enable_windows_terminal_colors();
    }

    void enable_windows_terminal_colors()
    {
#ifdef PLATFORM_WINDOWS
        HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE)
            return;

        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode))
            return;
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    }

    static constexpr std::string_view colour_for(LogLevel l)
    {
        switch (l) {
            case LogLevel::Debug: return "\033[36m";  // cyan
            case LogLevel::Info:  return "\033[32m";  // green
            case LogLevel::Warn:  return "\033[33m";  // yellow
            case LogLevel::Error: return "\033[31m";  // red
        }
        return "";
    }

    static constexpr std::string_view label_for(LogLevel l)
    {
        switch (l) {
            case LogLevel::Debug: return "DBG";
            case LogLevel::Info:  return "INF";
            case LogLevel::Warn:  return "WRN";
            case LogLevel::Error: return "ERR";
        }
        return "???";
    }

    std::mutex mu_;
    LogLevel level_ = LogLevel::Info;
};


#define LOG_DEBUG(tag, ...) Logger::instance().log(LogLevel::Debug, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  Logger::instance().log(LogLevel::Info,  tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  Logger::instance().log(LogLevel::Warn,  tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) Logger::instance().log(LogLevel::Error, tag, __VA_ARGS__)
