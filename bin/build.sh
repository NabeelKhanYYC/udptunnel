#!/bin/bash

# UDP Tunnel Build Script (Make + CMake)
# Usage: ./build.sh [target]
# Available targets: build, clean, all, debug, install

set -e

TARGET=${1:-build}

# Check if we have required build tools
check_dependencies() {
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        echo "Error: No C compiler found (gcc or clang required)"
        exit 1
    fi
    
    if ! command -v cmake &> /dev/null; then
        echo "Error: cmake not found"
        exit 1
    fi
    
    if ! command -v make &> /dev/null; then
        echo "Error: make not found"
        exit 1
    fi
    
    if ! command -v pkg-config &> /dev/null; then
        echo "Warning: pkg-config not found, systemd support may be disabled"
    fi
}

# Get script directory and ensure we're in the right place
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Create build directory if it doesn't exist
mkdir -p ../build/output/objs

# Change to build directory for all CMake operations
cd ../build

# Configure CMake if cache doesn't exist or is corrupted
configure_cmake() {
    if [ ! -f "CMakeCache.txt" ] || ! cmake --build . --dry-run &>/dev/null; then
        echo "Configuring CMake..."
        cmake .
    fi
}

case $TARGET in
    build)
        echo "Running builds..."
        check_dependencies
        echo "1. Running make build..."
        make
        echo "2. Running CMake build..."
        configure_cmake
        cmake --build .
        ;;
    clean)
        echo "Cleaning build artifacts..."
        make clean
        if [ -f "CMakeCache.txt" ]; then
            cmake --build . --target clean
        fi
        rm -rf output/* CMakeCache.txt CMakeFiles/
        ;;
    all)
        echo "Running clean then build..."
        check_dependencies
        make clean
        if [ -f "CMakeCache.txt" ]; then
            cmake --build . --target clean
        fi
        rm -rf output/* CMakeCache.txt CMakeFiles/
        echo "1. Running make build..."
        make
        echo "2. Configuring and running CMake build..."
        cmake .
        cmake --build .
        ;;
    debug)
        echo "Running debug builds..."
        check_dependencies
        echo "1. Running make debug build..."
        CFLAGS="-g -O0 -DDEBUG" make
        echo "2. Running CMake debug build..."
        rm -f CMakeCache.txt
        cmake -DCMAKE_BUILD_TYPE=Debug .
        cmake --build .
        ;;
    install)
        echo "Installing udptunnel..."
        check_dependencies
        make install
        configure_cmake
        cmake --build . --target install
        ;;
    *)
        echo "Usage: $0 [build|clean|all|debug|install]"
        echo ""
        echo "Available targets:"
        echo "  build   - Run both make and CMake builds (default)"
        echo "  clean   - Clean all build artifacts"
        echo "  all     - Clean then build both"
        echo "  debug   - Build both with debug flags"
        echo "  install - Install from both build systems"
        echo ""
        echo "Requirements:"
        echo "  - C compiler (gcc or clang)"
        echo "  - cmake"
        echo "  - make"
        echo "  - pkg-config (optional, for systemd support)"
        echo "  - libsystemd-dev (optional, for systemd support)"
        exit 1
        ;;
esac

if [ "$TARGET" != "clean" ] && [ -f "./output/udptunnel" ]; then
    echo "Build completed successfully!"
    echo "Binary: $SCRIPT_DIR/../build/output/udptunnel"
fi