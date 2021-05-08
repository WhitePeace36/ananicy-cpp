cmake_minimum_required(VERSION 3.17)

set(VERSION "0.0.0")
set(COMMIT_HASH "")
set(FULL_VERSION "${VERSION}")
if(VERSION STREQUAL "0.0.0")
    message(STATUS "No version defined, fetching one from git")
    find_package(Git)
    if(Git_FOUND)
        message(STATUS "Git found: ${GIT_EXECUTABLE}")

        execute_process(
                COMMAND ${GIT_EXECUTABLE} describe --long
                OUTPUT_VARIABLE OUTPUT
                RESULT_VARIABLE STATUS
        )
        if (STATUS EQUAL 0)
            string(REGEX REPLACE "v([^-]+)-([0-9]+)-([a-zA-Z0-9]+).+" "\\1.r\\2.\\3" FULL_VERSION "${OUTPUT}")
            set(VERSION ${CMAKE_MATCH_1})
            set(COMMIT_COUNT ${CMAKE_MATCH_2})
            set(COMMIT_HASH ${CMAKE_MATCH_3})
            message(STATUS "Version from git: ${FULL_VERSION}")
        endif()
    endif()
endif()
message("Version: ${VERSION}")

project(ananicy-cpp VERSION ${VERSION})

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)
option(USE_EXTERNAL_SPDLOG "Use external Spdlog library" OFF)
option(USE_EXTERNAL_FMTLIB "Use external fmt library" OFF)
option(ENABLE_SYSTEMD "Include systemd headers and install service file" ON)
option(OPTIMIZE_FOR_NATIVE_MICROARCH "Enable -march=native flag on the CXX compiler. This makes build NOT portable." OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MESSAGE_CONTEXT_SHOW ON)

set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

configure_file(include/version.hpp.in ${CMAKE_SOURCE_DIR}/include/version.hpp)

# Compiler checks
message(STATUS "Checking compiler flags support")
set(CMAKE_MESSAGE_CONTEXT "compiler checks")
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-g3" CXX_ACCEPTS_G3)
check_cxx_compiler_flag("-g" CXX_ACCEPTS_G)
check_cxx_compiler_flag("-Og" CXX_ACCEPTS_OG)
check_cxx_compiler_flag("-flto" CXX_ACCEPTS_FLTO)
set(CMAKE_MESSAGE_CONTEXT "")

if(CXX_ACCEPTS_G3)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
elseif(CXX_ACCEPTS_G)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
endif()

if(CXX_ACCEPTS_OG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1")
endif()

if(CXX_ACCEPTS_FLTO)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
endif()

include(FetchContent)


list(APPEND SOURCES
        src/main.cpp
        src/utils.cpp
        src/rules.cpp
        src/worker.cpp
        src/config.cpp)

list(APPEND HEADERS
        include/config.hpp
        include/priority.hpp
        include/process.hpp
        include/rules.hpp
        include/utils.hpp
        include/worker.hpp
        include/version.hpp)

list(APPEND LINUX_SOURCES
        src/os/linux/priority.cpp
        src/os/linux/process.cpp
        src/os/linux/ioprio.h)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    message(STATUS "Linux system detected, adding specific sources files")
    list(APPEND SOURCES ${LINUX_SOURCES})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message(FATAL_ERROR "macOS (Darwin) system detected, but is not supported yet. Please contribute to this project at https://gitlab.com/aviallon/ananicy-cpp if you need it.")
else()
    message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} system detected, but is not supported yet. Please contribute to this project at https://gitlab.com/aviallon/ananicy-cpp if you need it.")
endif()

add_executable(ananicy-cpp ${HEADERS} ${SOURCES})


# threading support
message(STATUS "Configuring threads")
set(CMAKE_MESSAGE_CONTEXT "threads")
find_package(Threads REQUIRED)
target_link_libraries(ananicy-cpp PRIVATE Threads::Threads)
set(CMAKE_MESSAGE_CONTEXT "")


# nlohmann_json
message(STATUS "Configuring nlohmann_json")
set(CMAKE_MESSAGE_CONTEXT "nlohmann_json")
if (USE_EXTERNAL_JSON)
    find_package(nlohmann_json 3.9 REQUIRED)
else()
    set(JSON_BuildTests Off)
    FetchContent_Declare(json
            GIT_REPOSITORY https://github.com/nlohmann/json
            GIT_TAG v3.9.1)

    FetchContent_MakeAvailable(json)
endif()
target_link_libraries(ananicy-cpp PRIVATE "nlohmann_json::nlohmann_json")
set(CMAKE_MESSAGE_CONTEXT "")

# std::format
include(CheckCXXSymbolExists)
message(STATUS "stdlib checks")
set(CMAKE_MESSAGE_CONTEXT "std")
check_cxx_symbol_exists(std::format "format" HAVE_STD_FORMAT)
if(NOT HAVE_STD_FORMAT)
    message(STATUS "std::format not available, using fmtlib as a replacement")
endif()
set(CMAKE_MESSAGE_CONTEXT "")

# fmt
if (NOT HAVE_STD_FORMAT)
    message(STATUS "Configuring fmtlib")
    set(CMAKE_MESSAGE_CONTEXT "fmtlib")
    if(USE_EXTERNAL_FMTLIB)
        find_package(fmt)
    else()
        FetchContent_Declare(fmtlib
                GIT_REPOSITORY https://github.com/fmtlib/fmt
                GIT_TAG 7.1.3)

        FetchContent_MakeAvailable(fmtlib)
    endif()
    target_link_libraries(ananicy-cpp PRIVATE fmt)
    target_include_directories(ananicy-cpp PRIVATE polyfills/format)
    target_sources(ananicy-cpp PRIVATE polyfills/format/format)
    set(CMAKE_MESSAGE_CONTEXT "")
endif()


# spdlog
message(STATUS "Configuring spdlog")
set(CMAKE_MESSAGE_CONTEXT "spdlog")
if(USE_EXTERNAL_SPDLOG)
    # Stand-alone build
    find_package(spdlog 1.8 REQUIRED)
else()
    FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog
            GIT_TAG v1.8.5)

    FetchContent_MakeAvailable(spdlog)
endif()
target_link_libraries(ananicy-cpp PRIVATE spdlog::spdlog)
set(CMAKE_MESSAGE_CONTEXT "")


# argparse
message(STATUS "Configuring argparse")
set(CMAKE_MESSAGE_CONTEXT "argparse")
FetchContent_Declare(argparse
        GIT_REPOSITORY https://github.com/jamolnng/argparse
        GIT_TAG cb70333358d0324961399546ed842a2233e17bbb)
# FetchContent_MakeAvailable(argparse)
if(NOT argparse_POPULATED)
    FetchContent_Populate(argparse)
    message(STATUS "Copy argparse.h to an include directory")
    file(COPY ${argparse_SOURCE_DIR}/argparse.h DESTINATION ${argparse_BINARY_DIR}/include)
    message(STATUS "Add argparse.h include dir to target's includes")
    target_include_directories(ananicy-cpp PRIVATE ${argparse_BINARY_DIR}/include)
endif()
set(CMAKE_MESSAGE_CONTEXT "")

if(ENABLE_SYSTEMD)
    message(STATUS "Configuring systemd")
    set(CMAKE_MESSAGE_CONTEXT "systemd")
    add_compile_definitions(ENABLE_SYSTEMD=1)
    target_link_libraries(ananicy-cpp PRIVATE "systemd")
    set(CMAKE_MESSAGE_CONTEXT "")
endif()

target_include_directories(ananicy-cpp PRIVATE "${CMAKE_HOME_DIRECTORY}/include")

include(GNUInstallDirs)
install(TARGETS ananicy-cpp
        RUNTIME DESTINATION bin)

if(ENABLE_SYSTEMD)
    configure_file(ananicy-cpp.service ananicy-cpp.service @ONLY)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ananicy-cpp.service
        DESTINATION lib/systemd/system/)
endif()