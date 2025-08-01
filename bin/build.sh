#!/bin/bash

# UDP Tunnel Native Build Script
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
mkdir -p build/objs

# Change to src directory for all make operations
cd ../src

case $TARGET in
    build)
        echo "Running native build..."
        check_dependencies
        make
        ;;
    clean)
        echo "Cleaning build artifacts..."
        make clean
        ;;
    all)
        echo "Running clean then build..."
        check_dependencies
        make clean && make
        ;;
    debug)
        echo "Running debug build..."
        check_dependencies
        CFLAGS="-g -O0 -DDEBUG" make
        ;;
    install)
        echo "Installing udptunnel..."
        check_dependencies
        make install
        ;;
    *)
        echo "Usage: $0 [build|clean|all|debug|install]"
        echo ""
        echo "Available targets:"
        echo "  build   - Native build (default)"
        echo "  clean   - Clean build artifacts"
        echo "  all     - Clean then build"
        echo "  debug   - Build with debug flags"
        echo "  install - Install to system"
        echo ""
        echo "Requirements:"
        echo "  - C compiler (gcc or clang)"
        echo "  - make"
        echo "  - pkg-config (optional, for systemd support)"
        echo "  - libsystemd-dev (optional, for systemd support)"
        exit 1
        ;;
esac

if [ "$TARGET" != "clean" ] && [ -f "../../build/udptunnel" ]; then
    echo "Build completed successfully!"
    echo "Binary: $SCRIPT_DIR/../build/udptunnel"
fi