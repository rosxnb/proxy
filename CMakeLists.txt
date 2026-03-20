cmake_minimum_required(VERSION 4.2)
project(tcp_proxy CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Platform detection ────────────────────────────────────────────────────────
if(APPLE)
    add_compile_definitions(PLATFORM_MACOS)
    message(STATUS "Platform: macOS (kqueue)")
elseif(UNIX)
    add_compile_definitions(PLATFORM_LINUX)
    message(STATUS "Platform: Linux (epoll)")
elseif(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS)
    message(STATUS "Platform: Windows (select)")
endif()

# ── Common sources ────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/Socket.cpp
    src/interceptor/InterceptorChain.cpp
    src/proxy/OriginalDst.cpp
    src/proxy/ProxyConfig.cpp
    src/proxy/ProxyServer.cpp
    src/proxy/TcpTunnel.cpp
    src/utils/ArgumentParser.cpp
)

# ── Platform-specific event loop ──────────────────────────────────────────────
if(APPLE)
    list(APPEND SOURCES src/platform/KqueueEventLoop.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/platform/EpollEventLoop.cpp)
elseif(WIN32)
    list(APPEND SOURCES src/platform/SelectEventLoop.cpp)
endif()

add_executable(tcp_proxy ${SOURCES})
target_include_directories(tcp_proxy PRIVATE src)

# ── Compiler warnings & optimisation ─────────────────────────────────────────
target_compile_options(tcp_proxy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2>
)

# ── Platform link libs ────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(tcp_proxy PRIVATE ws2_32)
endif()
