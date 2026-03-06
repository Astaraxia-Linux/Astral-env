cmake_minimum_required(VERSION 3.28)
project(astral-env VERSION 5.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++23 features
add_compile_options(-std=c++23)

# Find required libraries
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Source files
set(UTIL_SOURCES
    src/util/tempdir.cpp
    src/util/tempfile.cpp
    src/util/process.cpp
    src/util/file.cpp
)

set(CONFIG_SOURCES
    src/config/conf.cpp
)

set(STORE_SOURCES
    src/store/store.cpp
    src/store/entry.cpp
)

set(FETCH_SOURCES
    src/fetch/fetcher.cpp
)

set(REPO_SOURCES
    src/repo/bin_repo.cpp
)

set(LOCK_SOURCES
    src/lock/resolver.cpp
    src/lock/lockfile.cpp
)

set(ENV_SOURCES
    src/env/activation.cpp
    src/env/shell.cpp
)

set(SYSTEM_SOURCES
    src/system/system_conf.cpp
    src/system/differ.cpp
    src/system/applier.cpp
)

set(GC_SOURCES
    src/gc/gc.cpp
)

set(SNAP_SOURCES
    src/snap/snap.cpp
)

set(ALL_SOURCES
    ${UTIL_SOURCES}
    ${CONFIG_SOURCES}
    ${STORE_SOURCES}
    ${FETCH_SOURCES}
    ${REPO_SOURCES}
    ${LOCK_SOURCES}
    ${ENV_SOURCES}
    ${SYSTEM_SOURCES}
    ${GC_SOURCES}
    ${SNAP_SOURCES}
    src/main.cpp
)

# Create the executable
add_executable(astral-env ${ALL_SOURCES})

# Include directories
target_include_directories(astral-env PRIVATE src)

# Link libraries
target_link_libraries(astral-env PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)

# Install target
install(TARGETS astral-env DESTINATION bin)

# Print configuration info
message(STATUS "astral-env version: ${PROJECT_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
