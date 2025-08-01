#!/bin/bash

# UDP Tunnel Docker Build Wrapper
# Usage: ./docker-build.sh [target]
# Available targets: build, clean, all, debug

set -e

TARGET=${1:-build}

case $TARGET in
    build)
        echo "Running normal build..."
        cd ../ && docker compose --profile build up build
        ;;
    clean)
        echo "Running clean..."
        cd ../ && docker compose --profile build up clean
        ;;
    all)
        echo "Running clean then build..."
        cd ../ && docker compose --profile build up all
        ;;
    debug)
        echo "Running debug build..."
        cd ../ && docker compose --profile build up debug
        ;;
    *)
        echo "Usage: $0 [build|clean|all|debug]"
        echo ""
        echo "Available targets:"
        echo "  build  - Normal build (default)"
        echo "  clean  - Clean only"
        echo "  all    - Clean then build"
        echo "  debug  - Build with debug flags"
        exit 1
        ;;
esac

echo "Build completed successfully!"