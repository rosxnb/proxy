#!/bin/bash

BUILD_TYPE_INPUT="${1:-Release}"

# POSIX-compliant lowercase conversion using tr
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE_INPUT" | tr '[:upper:]' '[:lower:]')

# Normalize to proper CMake build type
case "$BUILD_TYPE_LOWER" in
    "release"|"r")
        BUILD_TYPE="Release"
        ;;
    "debug"|"d")
        BUILD_TYPE="Debug"
        ;;
    "relwithdebinfo"|"rwdi")
        BUILD_TYPE="RelWithDebInfo"
        ;;
    "minsizerel"|"msr")
        BUILD_TYPE="MinSizeRel"
        ;;
    *)
        echo "❌ Error: Invalid build type '$1'"
        echo "Valid options: Release (r), Debug (d), RelWithDebInfo (rwdi), MinSizeRel (msr)"
        exit 1
        ;;
esac

get_cpu_cores() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sysctl -n hw.logicalcpu 2>/dev/null
    else
        nproc 2>/dev/null || grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 4
    fi
}
CPU_CORES=$(get_cpu_cores)

echo "=============================================================="
echo "🔨 Building with configuration: $BUILD_TYPE, cores: $CPU_CORES"
echo "=============================================================="

cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
if [ $? -ne 0 ]; then
    echo "❌ CMake configuration failed"
    exit 1
fi
echo "==================================="
echo "✅ CMake Generation step sucessful!"
echo "==================================="
echo

cmake --build build -j "$CPU_CORES"
if [ $? -eq 0 ]; then
    echo "========================================================================"
    echo "✅ Build successful! (Configuration: $BUILD_TYPE, CPU Cores: $CPU_CORES)"
    echo "========================================================================"
else
    echo "❌ Build failed"
    exit 1
fi
