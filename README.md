# UDP Tunnel 

UDP Tunnel is a fork of Marco d'Itri's (@rfc1036) [rfc1036/udptunnel](https://github.com/rfc1036/udptunnel) focused on making it more accessible through improved build processes and containerization. He did excellent work when he created the original source code which is why it has truly stood the test of time.

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

Objects go to `build/objs/` and the final binary is in `build/udptunnel`.

### Using the Binary Outside Docker

The Docker build process automatically makes the `udptunnel` binary available on your host system through volume binding. After building with Docker, the binary is directly accessible:

1. **Build using Docker**:
   ```bash
   ./bin/docker-build.sh build
   ```

2. **Use the binary directly from the host**:
   ```bash
   # Binary is available at ./build/udptunnel
   ./build/udptunnel --help
   
   # Copy to system path if needed
   sudo cp ./build/udptunnel /usr/local/bin/
   ```

The volume binding in docker-compose.yml maps `./build:/opt/build/build:rw`, so build artifacts are automatically available on the host without needing to extract them from containers.

### Advanced 

#### Docker Build Targets 

You can also run `docker compose --profile build up <service>` directly with services: `build`, `clean`, `all`, `debug`.

#### Native Build

For native building on your host system:

```bash
./bin/build.sh [target]
```

Available targets:
- `build` - Native build (default)
- `clean` - Clean build artifacts
- `all` - Clean then build
- `debug` - Build with debug flags
- `install` - Install to system (native builds only)

Requirements: C compiler (gcc/clang), make, pkg-config (optional), libsystemd-dev (optional)

## Configuration

UDP Tunnel can be configured through environment variables when using Docker containers. For detailed configuration options, see the `.env.example` file which contains comprehensive documentation for all available environment variables including:

- **UDPTUNNEL_MODE**: Server or client mode operation
- **UDPTUNNEL_SOURCE_HOST**: Source host binding configuration
- **UDPTUNNEL_SOURCE_PORT**: Source port binding configuration  
- **UDPTUNNEL_DESTINATION_HOST**: Target destination host settings
- **UDPTUNNEL_DESTINATION_PORT**: Target destination port settings
- **UDPTUNNEL_TIMEOUT**: Connection timeout settings
- **UDPTUNNEL_VERBOSE**: Logging verbosity levels

Copy `.env.example` to `.env` and modify the values according to your needs when using `docker-compose`.

## Usage Examples

Here are common configuration examples for different use cases:

### TCP-to-UDP Server
Accept TCP connections and relay to UDP service:
```bash
UDPTUNNEL_MODE=server
UDPTUNNEL_SOURCE_HOST=0.0.0.0
UDPTUNNEL_SOURCE_PORT=8080
UDPTUNNEL_DESTINATION_HOST=target-udp-service
UDPTUNNEL_DESTINATION_PORT=9090
UDPTUNNEL_TIMEOUT=300
UDPTUNNEL_VERBOSE=1
```

### UDP-to-TCP Client
Accept UDP packets and relay via TCP:
```bash
UDPTUNNEL_MODE=client
UDPTUNNEL_SOURCE_HOST=0.0.0.0
UDPTUNNEL_SOURCE_PORT=9090
UDPTUNNEL_DESTINATION_HOST=tcp-server
UDPTUNNEL_DESTINATION_PORT=8080
UDPTUNNEL_TIMEOUT=600
UDPTUNNEL_VERBOSE=2
```

### Simple TCP-to-UDP Relay (Minimal Config)
Basic relay with minimal configuration:
```bash
UDPTUNNEL_MODE=server
UDPTUNNEL_SOURCE_PORT=8080
UDPTUNNEL_DESTINATION_HOST=backend
UDPTUNNEL_DESTINATION_PORT=5000
```

### Debug Configuration
High verbosity for troubleshooting:
```bash
UDPTUNNEL_MODE=client
UDPTUNNEL_SOURCE_HOST=0.0.0.0
UDPTUNNEL_SOURCE_PORT=7000
UDPTUNNEL_DESTINATION_HOST=debug-server
UDPTUNNEL_DESTINATION_PORT=7001
UDPTUNNEL_VERBOSE=3
```

## Project Structure

- Build scripts are located in `bin/` folder
- Source files are mounted read-only in the container
- Build artifacts are mapped directly to the host `build/` folder
- Makefile is located in `src/` folder