cmake_minimum_required (VERSION 3.10)

# Set the actual source directory
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

project ("udptunnel" "C")

set (CMAKE_PROJECT_DESCRIPTION "UDP tunnel over TCP")
set (CMAKE_PROJECT_HOMEPAGE_URL "https://github.com/NabeelKhanYYC/udptunnel")
set (PROJECT_VERSION "1.2.2")

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

# Add architecture-specific flags if needed
if(TARGET_ARCHITECTURE STREQUAL "armhf")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")
elseif(TARGET_ARCHITECTURE STREQUAL "arm64")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output/${TARGET_ARCHITECTURE}")
set (CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/output/${TARGET_ARCHITECTURE}")

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

add_executable (${PROJECT_NAME} ${SOURCES})
target_include_directories (${PROJECT_NAME} PRIVATE "../src")

# Find and link systemd if available (dynamically)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SYSTEMD libsystemd)
    if(SYSTEMD_FOUND)
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SYSTEMD_SD_DAEMON_H)
        target_include_directories(${PROJECT_NAME} PRIVATE ${SYSTEMD_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME} ${SYSTEMD_LIBRARIES})
        target_link_directories(${PROJECT_NAME} PRIVATE ${SYSTEMD_LIBRARY_DIRS})
        target_compile_options(${PROJECT_NAME} PRIVATE ${SYSTEMD_CFLAGS_OTHER})
    endif()
endif()

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include (CPackConfig.cmake)
include (CPack)
