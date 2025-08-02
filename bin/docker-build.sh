#!/bin/bash

# UDP Tunnel Docker Build Wrapper
# Usage: ./docker-build.sh [target]
# Available targets: build, clean, all, debug

set -e

TARGET=${1:-build}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Function to check if all Docker images are built and synchronized
check_docker_images() {
    echo "Checking Docker images..."
    
    # All services that use the build profile
    SERVICES="build clean all debug release"
    CREATION_TIMES=()
    
    # Check if all images exist and collect creation times
    for service in $SERVICES; do
        IMAGE_ID=$(docker images -q "udptunnel-${service}" 2>/dev/null)
        if [ -z "$IMAGE_ID" ]; then
            echo "    Warning: ${service} - Missing Docker image"
            return 1
        else
            echo "    Found  : ${service}"
        fi
        
        CREATION_TIME=$(docker images --format "{{.CreatedAt}}" "udptunnel-${service}" 2>/dev/null)
        CREATION_TIMES+=("$CREATION_TIME")
    done
    
    # Simplified check: just verify key images exist and are relatively recent
    # Focus on build vs release since those are the main problem containers
    BUILD_TIME=$(docker images --format "{{.CreatedSince}}" udptunnel-build 2>/dev/null)
    RELEASE_TIME=$(docker images --format "{{.CreatedSince}}" udptunnel-release 2>/dev/null)
    
    # Check if either image is older than 1 day
    if echo "$BUILD_TIME" | grep -q "days\|weeks\|months"; then
        echo "Warning: Build image is more than 1 day old: $BUILD_TIME"
        return 1
    fi
    
    if echo "$RELEASE_TIME" | grep -q "days\|weeks\|months"; then
        echo "Warning: Release image is more than 1 day old: $RELEASE_TIME"
        return 1
    fi
    
    echo "All Docker images are present and synchronized"
    return 0
}

# Check Docker images and rebuild if necessary
if ! check_docker_images; then
    echo "Rebuilding all Docker images..."
    cd "$SCRIPT_DIR/../" && docker compose --profile build build --no-cache
    echo "Images rebuilt. Relaunching script..."
    exec "$0" "$@"
fi

# Images are synchronized, proceed with build
echo "Building Docker image..."
cd "$SCRIPT_DIR/../" && docker compose --profile build build build

case $TARGET in
    build)
        echo "Running normal build..."
        cd "$SCRIPT_DIR/../" && docker compose --profile build up build
        BUILD_EXIT_CODE=$?
        ;;
    clean)
        echo "Running clean..."
        cd "$SCRIPT_DIR/../" && docker compose --profile build up clean
        BUILD_EXIT_CODE=$?
        ;;
    all)
        echo "Running clean then build..."
        cd "$SCRIPT_DIR/../" && docker compose --profile build up all
        BUILD_EXIT_CODE=$?
        ;;
    debug)
        echo "Running debug build..."
        cd "$SCRIPT_DIR/../" && docker compose --profile build up debug
        BUILD_EXIT_CODE=$?
        ;;
    release)
        echo "Running release build for all architectures..."
        cd "$SCRIPT_DIR/../" && docker compose --profile build up release
        BUILD_EXIT_CODE=$?
        ;;
    *)
        echo "Usage: $0 [build|clean|all|debug|release]"
        echo ""
        echo "Available targets:"
        echo "  build   - Normal build (default)"
        echo "  clean   - Clean only"
        echo "  all     - Clean then build"
        echo "  debug   - Build with debug flags"
        echo "  release - Build packages for all architectures"
        exit 1
        ;;
esac

if [ $BUILD_EXIT_CODE -eq 0 ]; then
    echo "Build completed successfully!"
else
    echo "Build failed with exit code $BUILD_EXIT_CODE"
    exit $BUILD_EXIT_CODE
fi