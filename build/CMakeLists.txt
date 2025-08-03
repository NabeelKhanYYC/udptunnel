cmake_minimum_required (VERSION 3.10)

# Set the actual source directory
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

project ("udptunnel" "C")

set (CMAKE_PROJECT_DESCRIPTION "UDP tunnel over TCP")
set (CMAKE_PROJECT_HOMEPAGE_URL "https://github.com/NabeelKhanYYC/udptunnel")
# Extract version from Makefile to ensure consistency
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Makefile" MAKEFILE_CONTENT)
string(REGEX MATCH "VERSION := ([0-9]+\\.[0-9]+\\.[0-9]+)" VERSION_MATCH "${MAKEFILE_CONTENT}")
set (PROJECT_VERSION "${CMAKE_MATCH_1}")

if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set (CMAKE_INSTALL_BINDIR "sbin" CACHE PATH "output directory for binaries")
endif ()

# Set architecture from environment variable or auto-detect
if(DEFINED ENV{UDPTUNNEL_ARCH} AND NOT "$ENV{UDPTUNNEL_ARCH}" STREQUAL "")
    set (TARGET_ARCHITECTURE "$ENV{UDPTUNNEL_ARCH}")
else()
    # Auto-detect architecture from CMAKE_SYSTEM_PROCESSOR (case-insensitive)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CMAKE_SYSTEM_PROCESSOR_LOWER)
    if(CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "x86_64|amd64")
        set (TARGET_ARCHITECTURE "amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "aarch64|arm64")
        set (TARGET_ARCHITECTURE "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "armv7l|armv7")
        set (TARGET_ARCHITECTURE "armhf")
    elseif(CMAKE_SYSTEM_PROCESSOR_LOWER MATCHES "i[3-6]86|x86")
        set (TARGET_ARCHITECTURE "i386")
    else()
        set (TARGET_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

# Detect host architecture for cross-compilation check
execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
if(HOST_ARCH_RAW MATCHES "x86_64|AMD64")
    set (HOST_ARCHITECTURE "amd64")
elseif(HOST_ARCH_RAW MATCHES "aarch64")
    set (HOST_ARCHITECTURE "arm64")
elseif(HOST_ARCH_RAW MATCHES "armv7l")
    set (HOST_ARCHITECTURE "armhf")
elseif(HOST_ARCH_RAW MATCHES "i[3-6]86")
    set (HOST_ARCHITECTURE "i386")
else()
    set (HOST_ARCHITECTURE "${HOST_ARCH_RAW}")
endif()

# Check for cross-compilation and display warnings
if(NOT TARGET_ARCHITECTURE STREQUAL HOST_ARCHITECTURE)
    message(STATUS "Warning: Cross-compilation required (host: ${HOST_ARCHITECTURE}, target: ${TARGET_ARCHITECTURE})")
    
    if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
        if(TARGET_ARCHITECTURE STREQUAL "arm64")
            message(STATUS "Suggested: Install cross-compiler with: apt-get install gcc-aarch64-linux-gnu")
            message(STATUS "Then run: UDPTUNNEL_ARCH=${TARGET_ARCHITECTURE} CC=aarch64-linux-gnu-gcc cmake .")
        elseif(TARGET_ARCHITECTURE STREQUAL "armhf")
            message(STATUS "Suggested: Install cross-compiler with: apt-get install gcc-arm-linux-gnueabihf")
            message(STATUS "Then run: UDPTUNNEL_ARCH=${TARGET_ARCHITECTURE} CC=arm-linux-gnueabihf-gcc cmake .")
        elseif(TARGET_ARCHITECTURE STREQUAL "i386")
            message(STATUS "Suggested: Install cross-compiler with: apt-get install gcc-multilib")
            message(STATUS "Then run: UDPTUNNEL_ARCH=${TARGET_ARCHITECTURE} CC='gcc -m32' cmake .")
        endif()
        message(STATUS "Warning: No cross-compiler specified (CMAKE_C_COMPILER)")
        message(STATUS "Build may fail or produce incorrect binary architecture")
    else()
        message(STATUS "Using cross-compiler: ${CMAKE_C_COMPILER}")
    endif()
else()
    message(STATUS "Native compilation (host: ${HOST_ARCHITECTURE}, target: ${TARGET_ARCHITECTURE})")
endif()

set (CMAKE_C_FLAGS "-O2 -std=c11 -Wall -D_POSIX_C_SOURCE=200809L")

# Conditional static linking
if(BUILD_TYPE STREQUAL "static")
    set (CMAKE_EXE_LINKER_FLAGS "-static")
endif()

# Set object output directory to architecture-specific location within objs
set (CMAKE_C_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output/objs/${TARGET_ARCHITECTURE}")
set (CMAKE_CXX_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output/objs/${TARGET_ARCHITECTURE}")

# Add architecture-specific flags if needed
if(TARGET_ARCHITECTURE STREQUAL "armhf")
    # Set ARM FPU from environment variable or default to neon
    if(DEFINED ENV{UDPTUNNEL_ARM_FPU} AND NOT "$ENV{UDPTUNNEL_ARM_FPU}" STREQUAL "")
        set (ARM_FPU "$ENV{UDPTUNNEL_ARM_FPU}")
    else()
        set (ARM_FPU "neon")
    endif()
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=${ARM_FPU} -mfloat-abi=hard")
elseif(TARGET_ARCHITECTURE STREQUAL "arm64")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
endif()

# Determine build type (static or dynamic)
if(DEFINED ENV{UDPTUNNEL_STATIC} AND ("$ENV{UDPTUNNEL_STATIC}" STREQUAL "1" OR "$ENV{UDPTUNNEL_STATIC}" STREQUAL "true"))
    set (BUILD_TYPE "static")
else()
    set (BUILD_TYPE "dynamic")
endif()

# Set versioned binary name
set (BINARY_NAME "udptunnel-${PROJECT_VERSION}-${TARGET_ARCHITECTURE}")
set (SYMLINK_NAME "udptunnel")

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output/${BUILD_TYPE}/${TARGET_ARCHITECTURE}")
set (CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/output/${BUILD_TYPE}/${TARGET_ARCHITECTURE}")

if (NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  set (CMAKE_BUILD_TYPE "Release")
endif ()

set (
  SOURCES
  "../src/libs/log/log.c"
  "../src/libs/network/network.c"
  "../src/udptunnel.c"
  "../src/libs/utils/utils.c"
)

add_executable (${BINARY_NAME} ${SOURCES})
# Create alias for compatibility
add_executable(${PROJECT_NAME} ALIAS ${BINARY_NAME})
target_include_directories (${BINARY_NAME} PRIVATE "../src")

# Find and link systemd if available (statically)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SYSTEMD libsystemd)
    if(SYSTEMD_FOUND)
        target_compile_definitions(${BINARY_NAME} PRIVATE HAVE_SYSTEMD_SD_DAEMON_H)
        target_include_directories(${BINARY_NAME} PRIVATE ${SYSTEMD_INCLUDE_DIRS})
        # Use appropriate libraries based on build type
        if(BUILD_TYPE STREQUAL "static")
            target_link_libraries(${BINARY_NAME} ${SYSTEMD_STATIC_LIBRARIES})
            # Add library search path for our stub library and link it
            target_link_directories(${BINARY_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/output/objs/${TARGET_ARCHITECTURE}")
            target_link_libraries(${BINARY_NAME} cap_stub)
        else()
            target_link_libraries(${BINARY_NAME} ${SYSTEMD_LIBRARIES})
        endif()
        target_link_directories(${BINARY_NAME} PRIVATE ${SYSTEMD_LIBRARY_DIRS})
        target_compile_options(${BINARY_NAME} PRIVATE ${SYSTEMD_CFLAGS_OTHER})
    endif()
endif()

# Install the versioned binary as 'udptunnel' for packages
install (TARGETS ${BINARY_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME udptunnel)

# Create symlink in build directory
add_custom_command(TARGET ${BINARY_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BINARY_NAME} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${SYMLINK_NAME}
    COMMENT "Creating symlink ${SYMLINK_NAME} -> ${BINARY_NAME}"
)

include (CPackConfig.cmake)
include (CPack)
