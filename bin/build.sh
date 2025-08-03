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
    
    # Create libcap stub library for static linking only
    if [ "$UDPTUNNEL_STATIC" = "1" ] || [ "$UDPTUNNEL_STATIC" = "true" ]; then
        echo "Creating libcap stub library for static linking..."
        mkdir -p ../build/output/objs/$TARGET_ARCH
        rm -f ../build/output/objs/$TARGET_ARCH/libcap_stub.a ../build/output/objs/$TARGET_ARCH/libcap_stub.o ../build/output/objs/$TARGET_ARCH/libcap_stub.c
        cat > ../build/output/objs/$TARGET_ARCH/libcap_stub.c << 'EOF'
// Minimal libcap stub implementation for static linking
#include <sys/types.h>
#include <stddef.h>
#include <errno.h>

typedef struct _cap_struct *cap_t;
typedef enum { CAP_EFFECTIVE=0, CAP_PERMITTED=1, CAP_INHERITABLE=2 } cap_flag_t;
typedef enum { CAP_CLEAR=0, CAP_SET=1 } cap_flag_value_t;

// Stub implementations that return safe defaults
cap_t cap_get_proc(void) { return NULL; }
int cap_set_proc(cap_t cap_p) { return 0; }
int cap_free(void *obj_d) { return 0; }
cap_t cap_init(void) { return NULL; }
cap_t cap_dup(cap_t cap_p) { return NULL; }
int cap_get_flag(cap_t cap_p, int cap, cap_flag_t flag, cap_flag_value_t *value_p) { if(value_p) *value_p = CAP_CLEAR; return 0; }
int cap_set_flag(cap_t cap_p, cap_flag_t flag, int ncap, const int *caps, cap_flag_value_t value) { return 0; }
int cap_compare(cap_t cap_a, cap_t cap_b) { return 0; }
EOF
        ${CC:-gcc} -c ../build/output/objs/$TARGET_ARCH/libcap_stub.c -o ../build/output/objs/$TARGET_ARCH/libcap_stub.o
        ar rcs ../build/output/objs/$TARGET_ARCH/libcap_stub.a ../build/output/objs/$TARGET_ARCH/libcap_stub.o
        echo "Libcap stub library created at ../build/output/objs/$TARGET_ARCH/libcap_stub.a"
    fi
}

# Get script directory and ensure we're in the right place
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Create build directories if they don't exist
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
        
        # Create default symlink to current build for system architecture
        HOST_ARCH=$(uname -m)
        case $HOST_ARCH in
            x86_64) DEFAULT_ARCH="amd64" ;;
            aarch64) DEFAULT_ARCH="arm64" ;;
            armv7l) DEFAULT_ARCH="armhf" ;;
            *) DEFAULT_ARCH="amd64" ;;
        esac
        
        # Determine build type
        if [ "$UDPTUNNEL_STATIC" = "1" ] || [ "$UDPTUNNEL_STATIC" = "true" ]; then
            BUILD_TYPE="static"
        else
            BUILD_TYPE="dynamic"
        fi
        
        cd output
        rm -f default udptunnel
        ln -sf "$BUILD_TYPE/$DEFAULT_ARCH" default
        ln -sf "default/udptunnel" udptunnel
        cd ..
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
        
        # Create default symlink to current build for system architecture
        HOST_ARCH=$(uname -m)
        case $HOST_ARCH in
            x86_64) DEFAULT_ARCH="amd64" ;;
            aarch64) DEFAULT_ARCH="arm64" ;;
            armv7l) DEFAULT_ARCH="armhf" ;;
            *) DEFAULT_ARCH="amd64" ;;
        esac
        
        # Determine build type
        if [ "$UDPTUNNEL_STATIC" = "1" ] || [ "$UDPTUNNEL_STATIC" = "true" ]; then
            BUILD_TYPE="static"
        else
            BUILD_TYPE="dynamic"
        fi
        
        cd output
        rm -f default udptunnel
        ln -sf "$BUILD_TYPE/$DEFAULT_ARCH" default
        ln -sf "default/udptunnel" udptunnel
        cd ..
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
        echo "Building release packages for all architectures (both static and dynamic)..."
        ARCHITECTURES="amd64 arm64 armhf"
        
        # Extract version from Makefile
        VERSION=$(grep '^VERSION := ' ../build/Makefile | sed 's/VERSION := //')
        echo "Building version: $VERSION"
        
        "$SCRIPT_DIR/$(basename "$0")" clean

        # Build both static and dynamic versions for each architecture
        for arch in $ARCHITECTURES; do
            echo ""
            echo "========================================="
            echo "Building dynamic binaries for architecture: $arch"
            echo "========================================="
            env -u CC UDPTUNNEL_ARCH=$arch UDPTUNNEL_STATIC=0 "$SCRIPT_DIR/$(basename "$0")" build
            
            echo ""
            echo "========================================="
            echo "Building static binaries for architecture: $arch"
            echo "========================================="
            env -u CC UDPTUNNEL_ARCH=$arch UDPTUNNEL_STATIC=1 "$SCRIPT_DIR/$(basename "$0")" build
        done
        
        # Create default symlink to dynamic build for system architecture
        HOST_ARCH=$(uname -m)
        case $HOST_ARCH in
            x86_64) DEFAULT_ARCH="amd64" ;;
            aarch64) DEFAULT_ARCH="arm64" ;;
            armv7l) DEFAULT_ARCH="armhf" ;;
            *) DEFAULT_ARCH="amd64" ;;
        esac
        
        cd output
        rm -f default udptunnel
        ln -sf "dynamic/$DEFAULT_ARCH" default
        ln -sf "default/udptunnel" udptunnel
        cd ..
        
        # Package release artifacts
        echo ""
        echo "========================================="
        echo "Packaging release artifacts..."
        echo "========================================="
        
        # Create release directory
        rm -rf output/release
        mkdir -p output/release
        
        # Copy all binaries and packages to release directory with proper naming
        for build_type in static dynamic; do
            for arch in $ARCHITECTURES; do
                if [ -d "output/$build_type/$arch" ]; then
                    # Copy binary
                    if [ -f "output/$build_type/$arch/udptunnel-$VERSION-$arch" ]; then
                        if [ "$build_type" = "static" ]; then
                            cp "output/$build_type/$arch/udptunnel-$VERSION-$arch" "output/release/udptunnel-static-$VERSION-$arch"
                        else
                            cp "output/$build_type/$arch/udptunnel-$VERSION-$arch" "output/release/udptunnel-$VERSION-$arch"
                        fi
                        echo "  Copied $build_type binary for $arch"
                    fi
                    
                    # Copy packages (DEB and RPM)
                    for pkg in output/$build_type/$arch/*.deb output/$build_type/$arch/*.rpm; do
                        if [ -f "$pkg" ]; then
                            pkg_basename=$(basename "$pkg")
                            if [ "$build_type" = "static" ]; then
                                # Insert "static" into package name: udptunnel-1.2.2.amd64.deb -> udptunnel-static-1.2.2.amd64.deb
                                new_pkg_name=$(echo "$pkg_basename" | sed "s/udptunnel-/udptunnel-static-/")
                            else
                                new_pkg_name="$pkg_basename"
                            fi
                            cp "$pkg" "output/release/$new_pkg_name"
                            echo "  Copied $build_type package: $new_pkg_name"
                        fi
                    done
                fi
            done
        done
        
        # Create source code archives using git
        echo ""
        echo "Creating source code archives..."
        
        # Check if git is available
        if ! command -v git &> /dev/null; then
            echo "Error: git command not found. Git is required for creating source archives."
            exit 1
        fi
        
        # Use /opt/src if available (Docker environment), otherwise current directory
        if [ -d "/opt/src/.git" ]; then
            SRC_DIR="/opt/src"
            echo "  Using source directory: $SRC_DIR"
        else
            SRC_DIR="$(pwd)/../"
            echo "  Using source directory: $SRC_DIR"
            # Check if we're in a git repository
            if ! (cd "$SRC_DIR" && git rev-parse --git-dir > /dev/null 2>&1); then
                echo "Error: Not in a git repository. Source archives require git repository."
                exit 1
            fi
        fi
        
        # Use git archive to create clean source exports
        echo "  Creating zip archive..."
        (cd "$SRC_DIR" && git archive --format=zip --prefix="udptunnel-$VERSION/" HEAD) > "output/release/udptunnel-$VERSION-source.zip"
        
        echo "  Creating tar.gz archive..."
        (cd "$SRC_DIR" && git archive --format=tar.gz --prefix="udptunnel-$VERSION/" HEAD) > "output/release/udptunnel-$VERSION-source.tar.gz"
        
        echo "  Source archives created successfully"
        
        echo ""
        echo "========================================="
        echo "Release build completed!"
        echo "========================================="
        echo "Release artifacts available in: output/release/"
        ls -la output/release/ 2>/dev/null || echo "  No release artifacts found"
        echo ""
        echo "Architecture-specific builds also available in:"
        for build_type in static dynamic; do
            for arch in $ARCHITECTURES; do
                if [ -d "output/$build_type/$arch" ]; then
                    echo "  $build_type/$arch: output/$build_type/$arch/"
                fi
            done
        done
        echo ""
        echo "Default symlink points to: output/default -> $(readlink output/default 2>/dev/null || echo 'not created')"
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
    echo "Binary: $SCRIPT_DIR/../build/output/udptunnel (symlink to versioned binary)"
fi