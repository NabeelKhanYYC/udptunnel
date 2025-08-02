prefix = /usr/local

# Determine absolute paths based on Makefile location
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR := $(PROJECT_ROOT)/src
BUILD_DIR := $(MAKEFILE_DIR)output/$(TARGET_ARCH)
OBJ_DIR := $(BUILD_DIR)/objs

# ==============================================================================
# ARCHITECTURE DETECTION AND CROSS-COMPILATION
# ==============================================================================

# Set architecture from environment variable or auto-detect
ifdef UDPTUNNEL_ARCH
    TARGET_ARCH := $(UDPTUNNEL_ARCH)
else
    # Auto-detect architecture
    UNAME_M := $(shell uname -m)
    ifeq ($(UNAME_M),x86_64)
        TARGET_ARCH := amd64
    else ifeq ($(UNAME_M),aarch64)
        TARGET_ARCH := arm64
    else ifeq ($(UNAME_M),armv7l)
        TARGET_ARCH := armhf
    else ifeq ($(findstring i,$(UNAME_M)),i)
        TARGET_ARCH := i386
    else
        TARGET_ARCH := $(UNAME_M)
    endif
endif

# Detect host architecture for cross-compilation check
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    HOST_ARCH := amd64
else ifeq ($(UNAME_M),aarch64)
    HOST_ARCH := arm64
else ifeq ($(UNAME_M),armv7l)
    HOST_ARCH := armhf
else ifeq ($(findstring i,$(UNAME_M)),i)
    HOST_ARCH := i386
else
    HOST_ARCH := $(UNAME_M)
endif

# Check for cross-compilation and warn if needed
ifneq ($(TARGET_ARCH),$(HOST_ARCH))
    $(info Warning: Cross-compilation required (host: $(HOST_ARCH), target: $(TARGET_ARCH)))
    ifndef CC
        ifeq ($(TARGET_ARCH),arm64)
            $(info Suggested: Install cross-compiler with: apt-get install gcc-aarch64-linux-gnu)
            $(info Then run: UDPTUNNEL_ARCH=$(TARGET_ARCH) CC=aarch64-linux-gnu-gcc make)
        else ifeq ($(TARGET_ARCH),armhf)
            $(info Suggested: Install cross-compiler with: apt-get install gcc-arm-linux-gnueabihf)
            $(info Then run: UDPTUNNEL_ARCH=$(TARGET_ARCH) CC=arm-linux-gnueabihf-gcc make)
        else ifeq ($(TARGET_ARCH),i386)
            $(info Suggested: Install cross-compiler with: apt-get install gcc-multilib)
            $(info Then run: UDPTUNNEL_ARCH=$(TARGET_ARCH) CC='gcc -m32' make)
        endif
        $(info Warning: No cross-compiler specified (CC variable))
        $(info Build may fail or produce incorrect binary architecture)
        $(info )
    else
        $(info Using cross-compiler: $(CC))
    endif
else
    $(info Native compilation (host: $(HOST_ARCH), target: $(TARGET_ARCH)))
endif

# ==============================================================================
# COMPILER FLAGS AND CONFIGURATION  
# ==============================================================================

CFLAGS ?= -g -O2

# Add architecture-specific flags if needed
ifeq ($(TARGET_ARCH),armhf)
    CFLAGS += -march=armv7-a
else ifeq ($(TARGET_ARCH),arm64)
    CFLAGS += -march=armv8-a
endif

# Build tools and configuration
INSTALL ?= install
PKG_CONFIG ?= pkg-config

# Systemd integration (optional)
ifeq ($(shell $(PKG_CONFIG) --exists libsystemd || echo NO),)
    DEFS += -DHAVE_SYSTEMD_SD_DAEMON_H $(shell $(PKG_CONFIG) --cflags libsystemd)
    LDADD += $(shell $(PKG_CONFIG) --libs libsystemd)
endif

# Preprocessor and include flags
CPPFLAGS += $(DEFS) $(INCLUDES) -I$(SRC_DIR)

# ==============================================================================
# BUILD TARGETS AND OBJECTS
# ==============================================================================

OBJECTS := $(OBJ_DIR)/log.o $(OBJ_DIR)/network.o $(OBJ_DIR)/utils.o $(OBJ_DIR)/udptunnel.o

.PHONY: all clean depend install

all: $(OBJ_DIR) depend $(BUILD_DIR)/udptunnel

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

install:
	$(INSTALL) -d $(BASEDIR)$(prefix)/sbin/
	$(INSTALL) -m 0755 $(BUILD_DIR)/udptunnel $(BASEDIR)$(prefix)/sbin/

clean:
	@echo "Cleaning build artifacts..."
	-rm -f $(BUILD_DIR)/Makefile.depend $(BUILD_DIR)/udptunnel
	-rm -f $(OBJECTS)
	-rm -rf $(OBJ_DIR)
	@echo "Clean completed."

$(OBJ_DIR)/udptunnel.o: $(SRC_DIR)/udptunnel.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/utils.o: $(SRC_DIR)/libs/utils/utils.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/log.o: $(SRC_DIR)/libs/log/log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/network.o: $(SRC_DIR)/libs/network/network.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/udptunnel: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD) $(LIBS)

depend: $(BUILD_DIR)/Makefile.depend
$(BUILD_DIR)/Makefile.depend: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM -MG $(SRC_DIR)/udptunnel.c $(SRC_DIR)/libs/*/*.c > $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD_DIR)/Makefile.depend
endif
