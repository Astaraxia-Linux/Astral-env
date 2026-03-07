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
    util/tempdir.cpp
    util/tempfile.cpp
    util/process.cpp
    util/file.cpp
)

set(CONFIG_SOURCES
    config/conf.cpp
)

set(STORE_SOURCES
    store/store.cpp
    store/entry.cpp
)

set(FETCH_SOURCES
    fetch/fetcher.cpp
)

set(REPO_SOURCES
    repo/bin_repo.cpp
)

set(LOCK_SOURCES
    lock/resolver.cpp
    lock/lockfile.cpp
)

set(ENV_SOURCES
    env/activation.cpp
    env/shell.cpp
)

set(SYSTEM_SOURCES
    system/system_conf.cpp
    system/differ.cpp
    system/applier.cpp
)

set(GC_SOURCES
    gc/gc.cpp
)

set(SNAP_SOURCES
    snap/snap.cpp
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
    main.cpp
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
