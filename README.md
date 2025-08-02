# UDP Tunnel 

UDP Tunnel is a fork of Marco d'Itri's ([@rfc1036](https://github.com/rfc1036)) [rfc1036/udptunnel](https://github.com/rfc1036/udptunnel) focused on making it more accessible through improved build processes and containerization. He did excellent work when he created the original source code which is why it has truly stood the test of time.

Feel free to send over any PR's for expanded capabilities or create issues for any features you'd like created or merged from any of the other forks.

# About

UDP Tunnel is a utility that encapsulates UDP packets within TCP connections, enabling UDP traffic to traverse networks that only allow TCP connections. This is particularly useful when you need to:

- **Traverse firewalls** that block UDP traffic but allow TCP connections
- **Create reliable UDP tunnels** over networks with unreliable UDP delivery
- **Connect UDP services** across networks with TCP-only connectivity policies
- **Bypass network restrictions** that prevent direct UDP communication between hosts

The tool operates in two modes: server mode (accepts TCP connections and relays to UDP) and client mode (accepts UDP packets and encapsulates them in TCP connections).

## Building

### Docker Build

Use the Docker wrapper script:

```bash
./bin/docker-build.sh [target]
```

Available targets:
- `build` - Normal build (default)
- `clean` - Clean only
- `all` - Clean then build
- `debug` - Build with debug flags

### Build Output

Objects go to `build/output/{arch}/objs/` and the final binary is in `build/output/{arch}/udptunnel`. Package files (DEB/RPM) are automatically created in `build/output/{arch}/` during the build process, where `{arch}` is the target architecture (amd64, arm64, armhf).

### Using the Binary Outside Docker

The Docker build process automatically makes the `udptunnel` binary available on your host system through volume binding. After building with Docker, the binary is directly accessible:

1. **Build using Docker**:
   ```bash
   ./bin/docker-build.sh build
   ```

2. **Use the binary directly from the host**:
   ```bash
   # Binaries are in architecture-specific directories: ./build/output/{arch}/udptunnel
   # For local builds, use your system architecture (typically amd64)
   ./build/output/amd64/udptunnel --help
   
   # Copy to system path if needed
   sudo cp ./build/output/amd64/udptunnel /usr/local/bin/
   ```

The volume binding in docker-compose.yml maps `./build:/opt/build/build:rw`, so build artifacts (including DEB/RPM packages) are automatically available on the host without needing to extract them from containers.

## Package Installation

The build process automatically creates DEB and RPM packages for easy system installation:

### Installing Built Packages

After building, you can install UDP Tunnel system-wide using the generated packages:

```bash
# For Debian/Ubuntu systems
sudo dpkg -i ./build/output/amd64/udptunnel-1.2.2.amd64.deb

# For RedHat/CentOS/Fedora systems  
sudo rpm -i ./build/output/amd64/udptunnel-1.2.2.amd64.rpm
```

### Package Features

- **System Integration**: Packages install to standard system paths
- **Systemd Support**: Includes systemd service integration when available
- **Clean Uninstall**: Standard package manager removal
- **Dependencies**: Automatically handles runtime dependencies

### Advanced 

#### Docker Build Targets 

You can also run `docker compose --profile build up <service>` directly with services: `build`, `clean`, `all`, `debug`.

#### Interactive Container Access

For debugging, development, or manual operations, you can access the containerized build environment interactively:

**Access container shell:**
```bash
# Interactive bash shell in build container
docker compose --profile build run --rm build bash

# Run specific commands in container
docker compose --profile build run --rm build ./bin/build.sh build
docker compose --profile build run --rm build which cmake
```

**Debug build issues:**
```bash
# Check what's available in the container
docker compose --profile build run --rm build ls -la /opt/build/

# Verify cmake installation and version
docker compose --profile build run --rm build cmake --version

# Check architecture detection
docker compose --profile build run --rm build uname -m

# Test cross-compilation setup
UDPTUNNEL_ARCH=arm64 docker compose --profile build run --rm build ./bin/build.sh build

# Check architecture-specific build outputs
docker compose --profile build run --rm build ls -la build/output/
```

**Development workflow:**
```bash
# Interactive development session
docker compose --profile build run --rm build bash

# Inside container, you can:
# - Edit files (if mounted with write access)
# - Run builds manually: ./bin/build.sh build
# - Inspect build artifacts: ls -la build/output/{arch}/
# - Debug compilation issues
# - Test different architectures
```

#### Native Build

For native building on your host system:

```bash
./bin/build.sh [target]
```

Available targets:
- `build` - Run both Make and CMake builds, create packages (default)
- `clean` - Clean all build artifacts from both build systems
- `all` - Clean then build both systems
- `debug` - Build both systems with debug flags
- `install` - Install from both build systems to system paths

The build script automatically:
- Runs both Make and CMake build processes
- Creates DEB and RPM packages in `build/output/{arch}/`
- Handles all dependencies and configuration

Requirements: C compiler (gcc/clang), cmake, make, pkg-config (optional), libsystemd-dev (optional)

### Multi-Architecture Support

UDP Tunnel supports building for multiple architectures. The build system automatically detects your system architecture, but you can override this with the `UDPTUNNEL_ARCH` environment variable.

**Supported architectures:**
- `amd64` - x86_64 (default for x86_64 systems)
- `arm64` - ARM64/AArch64 
- `armhf` - ARM v7 hard-float

**Usage examples:**
```bash
# Auto-detect architecture (default behavior)
./bin/build.sh build

# Override architecture for cross-compilation
UDPTUNNEL_ARCH=arm64 ./bin/build.sh build

# Build with cross-compiler
UDPTUNNEL_ARCH=armhf CC=arm-linux-gnueabihf-gcc ./bin/build.sh build

# Docker build for specific architecture
UDPTUNNEL_ARCH=arm64 docker-compose --profile build up build
```

### Cross-Compilation Detection

The build system automatically detects when cross-compilation is required and provides helpful guidance:

**When you specify a different target architecture:**
```bash
# Example: Building ARM64 on x86_64 system
UDPTUNNEL_ARCH=arm64 ./bin/build.sh build
```

**The build system will warn you and suggest the appropriate cross-compiler:**
```
Warning: Cross-compilation required (host: amd64, target: arm64)
Suggested: Install cross-compiler with: apt-get install gcc-aarch64-linux-gnu
Then run: UDPTUNNEL_ARCH=arm64 CC=aarch64-linux-gnu-gcc ./bin/build.sh build
```

**Supported cross-compiler suggestions:**
- **ARM64**: `gcc-aarch64-linux-gnu` → `CC=aarch64-linux-gnu-gcc`
- **ARM v7**: `gcc-arm-linux-gnueabihf` → `CC=arm-linux-gnueabihf-gcc`

**Note:** 32-bit x86 (i386) architecture support has been removed due to cross-compiler compatibility issues.

The generated packages and binaries will be named according to the target architecture (e.g., `udptunnel-1.2.2.arm64.deb`) and organized in architecture-specific directories (`build/output/{arch}/`).

## Configuration

UDP Tunnel can be configured through environment variables when using Docker containers. For detailed configuration options, see the `.env.example` file which contains comprehensive documentation for all available environment variables.

Copy `.env.example` to `.env` and modify the values according to your needs when using `docker-compose`.

## Usage Examples

### Binary Usage

Direct command line usage with the built or installed udptunnel binary.

#### TCP-to-UDP Server Mode
Accept TCP connections and relay to UDP service:
```bash
# Basic server: listen on TCP port 8080, relay to UDP port 9090 on target-host
./build/output/amd64/udptunnel -s 0.0.0.0:8080 target-host:9090

# With timeout and verbose logging
./build/output/amd64/udptunnel -s 0.0.0.0:8080 target-host:9090 -t 300 -v

# Minimal configuration (listen on all interfaces)
./build/output/amd64/udptunnel -s :8080 backend:5000
```

#### UDP-to-TCP Client Mode
Accept UDP packets and encapsulate in TCP connections:
```bash
# Basic client: listen on UDP port 9090, relay via TCP to port 8080 on tcp-server
./build/output/amd64/udptunnel -c 0.0.0.0:9090 tcp-server:8080

# With debug verbosity (multiple -v flags increase verbosity)
./build/output/amd64/udptunnel -c 0.0.0.0:9090 tcp-server:8080 -v -v -v

# With timeout settings
./build/output/amd64/udptunnel -c :7000 debug-server:7001 -t 600 -v
```

#### Command Line Options
```bash
# Get help and see all available options
./build/output/amd64/udptunnel --help

# Common patterns:
# -s <local:port> <remote:port>  # Server mode
# -c <local:port> <remote:port>  # Client mode  
# -t <seconds>                   # Timeout
# -v                             # Verbose (repeat for more verbosity)
```

### Container Usage

Environment variable configuration for Docker containers and docker-compose.

#### TCP-to-UDP Server (Container)
Configure via environment variables in `.env` file or docker-compose:
```bash
# In .env file or docker-compose environment
UDPTUNNEL_MODE=server
UDPTUNNEL_SOURCE_HOST=0.0.0.0
UDPTUNNEL_SOURCE_PORT=8080
UDPTUNNEL_DESTINATION_HOST=target-udp-service
UDPTUNNEL_DESTINATION_PORT=9090
UDPTUNNEL_TIMEOUT=300
UDPTUNNEL_VERBOSE=1
```

#### UDP-to-TCP Client (Container)
Client mode configuration:
```bash
# In .env file or docker-compose environment
UDPTUNNEL_MODE=client
UDPTUNNEL_SOURCE_HOST=0.0.0.0
UDPTUNNEL_SOURCE_PORT=9090
UDPTUNNEL_DESTINATION_HOST=tcp-server
UDPTUNNEL_DESTINATION_PORT=8080
UDPTUNNEL_TIMEOUT=600
UDPTUNNEL_VERBOSE=2
```

#### Minimal Container Configuration
```bash
# Basic server relay
UDPTUNNEL_MODE=server
UDPTUNNEL_SOURCE_PORT=8080
UDPTUNNEL_DESTINATION_HOST=backend
UDPTUNNEL_DESTINATION_PORT=5000
```

## Project Structure

- Build scripts are located in `bin/` folder
- Source files are mounted read-only in the container
- Build artifacts are mapped directly to the host `build/` folder
- Makefile is located in `src/` folder