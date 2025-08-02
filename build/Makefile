prefix = /usr/local

# Determine absolute paths based on Makefile location
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR := $(PROJECT_ROOT)/src
BUILD_DIR := $(MAKEFILE_DIR)output
OBJ_DIR := $(BUILD_DIR)/objs

CFLAGS ?= -g -O2

INSTALL ?= install
PKG_CONFIG ?= pkg-config

ifeq ($(shell $(PKG_CONFIG) --exists libsystemd || echo NO),)
DEFS += -DHAVE_SYSTEMD_SD_DAEMON_H $(shell $(PKG_CONFIG) --cflags libsystemd)
LDADD += $(shell $(PKG_CONFIG) --libs libsystemd)
endif

CPPFLAGS += $(DEFS) $(INCLUDES) -I$(SRC_DIR)

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
