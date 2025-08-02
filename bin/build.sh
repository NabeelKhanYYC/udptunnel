#!/bin/bash

# UDP Tunnel Build Script (Make + CMake)
# Usage: ./build.sh [target]
# Available targets: build, clean, all, debug, install

set -e

TARGET=${1:-build}

# Normalize architecture name (case-insensitive)
normalize_arch() {
    local arch=$(echo "$1" | tr '[:upper:]' '[:lower:]')
    case $arch in
        x86_64|amd64) echo "amd64" ;;
        aarch64|arm64) echo "arm64" ;;
        armv7l|armv7) echo "armhf" ;;
        *) echo "$arch" ;;
    esac
}

# Detect target architecture
detect_target_arch() {
    if [ -n "$UDPTUNNEL_ARCH" ]; then
        TARGET_ARCH="$UDPTUNNEL_ARCH"
    else
        # Auto-detect from system
        HOST_ARCH=$(uname -m)
        TARGET_ARCH=$(normalize_arch "$HOST_ARCH")
    fi
}

# Check if cross-compilation is required
check_cross_compilation() {
    HOST_ARCH=$(uname -m)
    HOST_ARCH_NORM=$(normalize_arch "$HOST_ARCH")
    
    if [ "$TARGET_ARCH" != "$HOST_ARCH_NORM" ]; then
        echo "Warning: Cross-compilation required (host: $HOST_ARCH_NORM, target: $TARGET_ARCH)"
        
        # Auto-detect and set cross-compiler if available, otherwise suggest installation
        if [ -z "$CC" ]; then
            case $TARGET_ARCH in
                arm64)
                    if command -v aarch64-linux-gnu-gcc &> /dev/null; then
                        export CC=aarch64-linux-gnu-gcc
                        echo "Auto-detected cross-compiler: $CC"
                    else
                        echo "Suggested: Install cross-compiler with: apt-get install gcc-aarch64-linux-gnu"
                        echo "Then run: UDPTUNNEL_ARCH=$TARGET_ARCH CC=aarch64-linux-gnu-gcc $0 $TARGET"
                        echo "Warning: No cross-compiler specified (CC environment variable)"
                        echo "Build may fail or produce incorrect binary architecture"
                        echo ""
                    fi
                    ;;
                armhf)
                    if command -v arm-linux-gnueabihf-gcc &> /dev/null; then
                        export CC=arm-linux-gnueabihf-gcc
                        echo "Auto-detected cross-compiler: $CC"
                    else
                        echo "Suggested: Install cross-compiler with: apt-get install gcc-arm-linux-gnueabihf"
                        echo "Then run: UDPTUNNEL_ARCH=$TARGET_ARCH CC=arm-linux-gnueabihf-gcc $0 $TARGET"
                        echo "Warning: No cross-compiler specified (CC environment variable)"
                        echo "Build may fail or produce incorrect binary architecture"
                        echo ""
                    fi
                    ;;
                amd64)
                    # Native amd64 compilation
                    ;;
            esac
        else
            echo "Using cross-compiler: $CC"
        fi
    else
        echo "Native compilation (host: $HOST_ARCH_NORM, target: $TARGET_ARCH)"
    fi
}

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

# Prepare build environment (dependencies, architecture, cross-compilation check)
prepare_build() {
    check_dependencies
    detect_target_arch
    check_cross_compilation
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
    # Use cache directory if available (for Docker read-only builds)
    if [ -d "cache" ] && [ -w "cache" ]; then
        CMAKE_BINARY_DIR="cache"
        echo "Using writable cache directory for CMake..."
        # For cross-compilation, always reconfigure CMake to ensure correct compiler
        if [ ! -f "cache/CMakeCache.txt" ] || ! cmake --build cache --dry-run &>/dev/null || [ -n "$CC" ]; then
            echo "Configuring CMake in cache directory..."
            echo "Debug: CC=$CC, CMAKE_C_COMPILER will be set to: $CC"
            rm -f cache/CMakeCache.txt  # Force reconfiguration for cross-compilation
            
            # Set pkg-config path for cross-compilation
            if [ "$TARGET_ARCH" = "arm64" ]; then
                export PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"
            elif [ "$TARGET_ARCH" = "armhf" ]; then
                export PKG_CONFIG_PATH="/usr/lib/arm-linux-gnueabihf/pkgconfig:$PKG_CONFIG_PATH"
            fi
            
            cd cache && CMAKE_C_COMPILER="$CC" cmake .. && cd ..
        fi
    else
        # Normal build directory
        # For cross-compilation, always reconfigure CMake to ensure correct compiler
        if [ ! -f "CMakeCache.txt" ] || ! cmake --build . --dry-run &>/dev/null || [ -n "$CC" ]; then
            echo "Configuring CMake..."
            echo "Debug: CC=$CC, CMAKE_C_COMPILER will be set to: $CC"
            rm -f CMakeCache.txt  # Force reconfiguration for cross-compilation
            
            # Set pkg-config path for cross-compilation
            if [ "$TARGET_ARCH" = "arm64" ]; then
                export PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"
            elif [ "$TARGET_ARCH" = "armhf" ]; then
                export PKG_CONFIG_PATH="/usr/lib/arm-linux-gnueabihf/pkgconfig:$PKG_CONFIG_PATH"
            fi
            
            CMAKE_C_COMPILER="$CC" cmake .
        fi
    fi
}

case $TARGET in
    build)
        echo "Running builds..."
        prepare_build
        echo "1. Running make build..."
        make
        echo "2. Running CMake build..."
        configure_cmake
        if [ -d "cache" ] && [ -w "cache" ]; then
            cmake --build cache
            echo "3. Creating packages..."
            cd cache && cpack && cd ..
        else
            cmake --build .
            echo "3. Creating packages..."
            cpack
        fi
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
        prepare_build
        make clean
        if [ -f "CMakeCache.txt" ]; then
            cmake --build . --target clean
        fi
        rm -rf output/* CMakeCache.txt CMakeFiles/
        echo "1. Running make build..."
        make
        echo "2. Configuring and running CMake build..."
        configure_cmake
        if [ -d "cache" ] && [ -w "cache" ]; then
            cmake --build cache
            echo "3. Creating packages..."
            cd cache && cpack && cd ..
        else
            cmake --build .
            echo "3. Creating packages..."
            cpack
        fi
        ;;
    debug)
        echo "Running debug builds..."
        prepare_build
        echo "1. Running make debug build..."
        CFLAGS="-g -O0 -DDEBUG" make
        echo "2. Running CMake debug build..."
        if [ -d "cache" ] && [ -w "cache" ]; then
            rm -f cache/CMakeCache.txt
            cd cache && cmake -DCMAKE_BUILD_TYPE=Debug .. && cd ..
            cmake --build cache
        else
            rm -f CMakeCache.txt
            cmake -DCMAKE_BUILD_TYPE=Debug .
            cmake --build .
        fi
        ;;
    install)
        echo "Installing udptunnel..."
        prepare_build
        make install
        configure_cmake
        if [ -d "cache" ] && [ -w "cache" ]; then
            cmake --build cache --target install
        else
            cmake --build . --target install
        fi
        ;;
    release)
        echo "Building release packages for all architectures..."
        ARCHITECTURES="amd64 arm64 armhf"
        
        "$SCRIPT_DIR/$(basename "$0")" clean

        for arch in $ARCHITECTURES; do
            echo ""
            echo "========================================="
            echo "Building for architecture: $arch"
            echo "========================================="
            
            # Recursively call this script with architecture set  
            # Clear CC to force auto-detection for each architecture
            env -u CC UDPTUNNEL_ARCH=$arch "$SCRIPT_DIR/$(basename "$0")" build
        done
        
        echo ""
        echo "========================================="
        echo "Release build completed!"
        echo "========================================="
        echo "All packages created in architecture-specific directories:"
        for arch in $ARCHITECTURES; do
            echo "  $arch: $SCRIPT_DIR/../build/output/$arch/"
            ls -la output/$arch/*.{deb,rpm} 2>/dev/null || echo "    No packages found for $arch"
        done
        ;;
    *)
        echo "Usage: $0 [build|clean|all|debug|install|release]"
        echo ""
        echo "Available targets:"
        echo "  build   - Run both make and CMake builds (default)"
        echo "  clean   - Clean all build artifacts"
        echo "  all     - Clean then build both"
        echo "  debug   - Build both with debug flags"
        echo "  install - Install from both build systems"
        echo "  release - Build packages for all architectures"
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