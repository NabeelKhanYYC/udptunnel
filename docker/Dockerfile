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