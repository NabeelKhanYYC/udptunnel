FROM ubuntu:latest AS base
ARG DEBIAN_FRONTEND=noninteractive  

# Update, Upgrade and install SSL Certificates
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y ca-certificates
RUN install -m 0755 -d /etc/apt/keyrings

# Common target for building
FROM base AS build

# Install Build Essentials
RUN apt-get install -y build-essential
RUN apt-get install -y pkg-config
RUN apt-get install -y libsystemd-dev
RUN apt-get install -y cmake
RUN apt-get install -y rpm

# Install cross-compilers for multi-architecture builds
RUN apt-get install -y gcc-aarch64-linux-gnu      # ARM64 cross-compiler
RUN apt-get install -y gcc-arm-linux-gnueabihf    # ARM v7 cross-compiler
RUN apt-get install -y crossbuild-essential-arm64 # ARM64 cross-build essentials
RUN apt-get install -y crossbuild-essential-armhf # ARM v7 cross-build essentials

# Enable multi-arch and install cross-compiled systemd libraries
RUN dpkg --add-architecture arm64
RUN dpkg --add-architecture armhf
RUN dpkg --print-foreign-architectures  # Debug: verify architectures were added
# Create new sources.list with architecture restrictions
RUN mv /etc/apt/sources.list /etc/apt/sources.list.bak
RUN rm -f /etc/apt/sources.list.d/ubuntu.sources
RUN echo "# AMD64 packages from main Ubuntu archive" > /etc/apt/sources.list
RUN echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ noble main restricted universe multiverse" >> /etc/apt/sources.list
RUN echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ noble-updates main restricted universe multiverse" >> /etc/apt/sources.list
RUN echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ noble-backports main restricted universe multiverse" >> /etc/apt/sources.list
RUN echo "deb [arch=amd64] http://security.ubuntu.com/ubuntu/ noble-security main restricted universe multiverse" >> /etc/apt/sources.list
RUN echo "" >> /etc/apt/sources.list
RUN echo "# ARM packages from Ubuntu ports" >> /etc/apt/sources.list
RUN echo "deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports/ noble main universe" >> /etc/apt/sources.list
RUN echo "deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports/ noble-updates main universe" >> /etc/apt/sources.list
RUN echo "deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports/ noble-security main universe" >> /etc/apt/sources.list
RUN apt-get update
RUN apt-cache search libsystemd-dev              # Debug: check if packages are available
RUN apt-get install -y libsystemd-dev:arm64      # ARM64 systemd library
RUN apt-get install -y libsystemd-dev:armhf      # ARM v7 systemd library
RUN apt-get install -y libcap-dev:arm64          # ARM64 capabilities library (systemd dependency)
RUN apt-get install -y libcap-dev:armhf          # ARM v7 capabilities library (systemd dependency)
RUN apt-get install -y libc6-dev:armhf           # ARM v7 C library development files
RUN dpkg -l | grep libsystemd                    # Debug: show what systemd packages are installed
# Restore original sources for future apt operations
RUN mv /etc/apt/sources.list.bak /etc/apt/sources.list

# Set working directory
RUN mkdir -p /opt/build
RUN cd /opt/build
WORKDIR /opt/build

# Target for building for containers
FROM build AS build-container

COPY ../ /opt/build
RUN ./bin/build.sh all

# Target for running a container
FROM base AS container

# Setup Timezone
RUN ln -fs /usr/share/zoneinfo/America/Edmonton /etc/localtime
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends tzdata

# Copy over binary
COPY --from=build-container /opt/build/build/output/udptunnel /usr/bin/udptunnel

# Create udptunnel user and group
RUN groupadd -r udptunnel && useradd -r -g udptunnel udptunnel

# Switch to udptunnel user
USER udptunnel

# Set the entrypoint so we can run this using docker run [options]
ENTRYPOINT ["/usr/bin/udptunnel"]

# Target for running a container via compose
FROM container AS compose

# Copy over the bootloader
USER root
COPY ../docker/bin/boot /boot/initialize
RUN chmod +x /boot/initialize
USER udptunnel

ENTRYPOINT []
CMD ["/bin/bash", "/boot/initialize"]