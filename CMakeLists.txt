cmake_minimum_required (VERSION 3.10)
project ("udptunnel" "C")

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message (FATAL_ERROR "In-source builds are not allowed")
endif ()

set (CMAKE_PROJECT_DESCRIPTION "UDP tunnel over TCP")
set (CMAKE_PROJECT_HOMEPAGE_URL "https://github.com/andrew-aladjev/udptunnel")
set (PROJECT_VERSION "1.2.0")

if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set (CMAKE_INSTALL_BINDIR "sbin" CACHE PATH "output directory for binaries")
endif ()

set (CMAKE_C_FLAGS "-O2 -std=c11 -Wall")

if (NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  set (CMAKE_BUILD_TYPE "Release")
endif ()

set (
  SOURCES
  "log.c"
  "network.c"
  "udptunnel.c"
  "utils.c"
)

add_executable (${PROJECT_NAME} ${SOURCES})
install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include (CPackConfig.cmake)
include (CPack)
