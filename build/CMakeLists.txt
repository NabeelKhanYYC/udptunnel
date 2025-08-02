cmake_minimum_required (VERSION 3.10)

# Set the actual source directory
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

project ("udptunnel" "C")

set (CMAKE_PROJECT_DESCRIPTION "UDP tunnel over TCP")
set (CMAKE_PROJECT_HOMEPAGE_URL "https://github.com/andrew-aladjev/udptunnel")
set (PROJECT_VERSION "1.2.2")

if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set (CMAKE_INSTALL_BINDIR "sbin" CACHE PATH "output directory for binaries")
endif ()

set (CMAKE_C_FLAGS "-O2 -std=c11 -Wall")

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/output")
set (CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/output")

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
