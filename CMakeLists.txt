cmake_minimum_required(VERSION 3.16)

set(VERSION "0.0.0" CACHE STRING "Override this value to specify the version string used by CMake and Ananicy-cpp")
set(COMMIT_HASH "")
set(COMMIT_COUNT 0)
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
            string(REGEX REPLACE "v([0-9A-Za-z.-]+)-([0-9]+)-([a-zA-Z0-9]+).+" "\\1.r\\2.\\3" FULL_VERSION "${OUTPUT}")
            set(VERSION ${FULL_VERSION})
            set(COMMIT_COUNT ${CMAKE_MATCH_2})
            set(COMMIT_HASH ${CMAKE_MATCH_3})
            message(STATUS "Version from git: ${FULL_VERSION}")
        endif()
    endif()
endif()
message("Version: ${VERSION}")
string(REGEX REPLACE "([0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9])?)(.*)" "\\1" VERSION_FOR_CMAKE "${VERSION}")
message(STATUS "version for cmake: ${VERSION_FOR_CMAKE}")

project(ananicy-cpp
        VERSION ${VERSION_FOR_CMAKE}
        LANGUAGES CXX)

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)
option(USE_EXTERNAL_SPDLOG "Use external Spdlog library" OFF)
option(USE_EXTERNAL_FMTLIB "Use external fmt library" OFF)
option(ENABLE_SYSTEMD "Include systemd headers and install service file" ON)
option(STATIC "Build static binary" OFF)
option(OPTIMIZE_FOR_NATIVE_MICROARCH "Enable -march=native flag on the CXX compiler. This makes build NOT portable." OFF)

set(CMAKE_CXX_STANDARD 20)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++2a -fconcepts")
    endif()
endif()
set(CMAKE_MESSAGE_CONTEXT_SHOW ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp)

# Compiler checks
message(STATUS "Checking compiler flags support")
set(CMAKE_MESSAGE_CONTEXT "compiler checks")
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-g3" CXX_ACCEPTS_G3)
check_cxx_compiler_flag("-g" CXX_ACCEPTS_G)
check_cxx_compiler_flag("-Og" CXX_ACCEPTS_OG)
check_cxx_compiler_flag("-flto" CXX_ACCEPTS_FLTO)
check_cxx_compiler_flag("-march=native" CXX_ACCEPTS_MARCH_NATIVE)
check_cxx_compiler_flag("-fsanitize=undefined" CXX_ACCEPTS_SANITIZE)
set(CMAKE_MESSAGE_CONTEXT "")

if(CXX_ACCEPTS_G3)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_DEBUG} -g3")
elseif(CXX_ACCEPTS_G)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_DEBUG} -g")
endif()

if(CXX_ACCEPTS_OG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1")
endif()

if(CXX_ACCEPTS_FLTO)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
endif()

if(CXX_ACCEPTS_SANITIZE AND NOT(STATIC))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif()

if(OPTIMIZE_FOR_NATIVE_MICROARCH AND CXX_ACCEPTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

if(STATIC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    set(BUILD_SHARED_LIBS OFF)
    if (ENABLE_SYSTEMD)
        message(FATAL_ERROR "Can't make a static build with systemd enabled, add -DENABLE_SYSTEMD=Off to cmake's invocation")
    endif()
    if(OPTIMIZE_FOR_NATIVE_MICROARCH)
        message(WARNING "It is not recommended to build a static binary with -march=native")
    endif()
endif()

include(FetchContent)


list(APPEND SOURCES
        src/main.cpp
        src/utils.cpp
        src/rules.cpp
        src/worker.cpp
        src/config.cpp
        src/utility/argument_parsing/argument_parser.cpp
        src/utility/argument_parsing/argument.cpp)

list(APPEND HEADERS
        include/config.hpp
        include/core/priority.hpp
        include/core/process.hpp
        include/core/rules.hpp
        include/utility/utils.hpp
        include/utility/synchronized_queue.hpp
        include/core/worker.hpp
        include/core/cgroups.hpp
        include/version.hpp
        include/service.hpp
        include/utility/backtrace.hpp
        include/utility/argument_parser.hpp
        include/utility/debug.hpp
        include/utility/process_info.hpp)

list(APPEND LINUX_SOURCES
        src/platform/linux/priority.cpp
        src/platform/linux/process.cpp
        src/platform/linux/syscalls.h
        src/platform/linux/backtrace.cpp
        src/platform/linux/cgroups.cpp
        src/platform/linux/debug.cpp
        src/platform/linux/process_info.cpp)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    message(STATUS "Linux system detected, adding specific sources files")
    list(APPEND SOURCES ${LINUX_SOURCES})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message(FATAL_ERROR "macOS (Darwin) system detected, but is not supported yet. Please contribute to this project at https://gitlab.com/aviallon/ananicy-cpp if you need it.")
else()
    message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} system detected, but is not supported yet. Please contribute to this project at https://gitlab.com/aviallon/ananicy-cpp if you need it.")
endif()

if(ENABLE_SYSTEMD)
    list(APPEND SOURCES
            src/platform/systemd/service.cpp)
else()
    list(APPEND SOURCES
            src/platform/generic/service.cpp)
endif()

add_executable(ananicy-cpp ${HEADERS} ${SOURCES})
target_sources(ananicy-cpp PRIVATE src include)

if(STATIC)
    set_target_properties(ananicy-cpp PROPERTIES LINK_SEARCH_START_STATIC ON)
    set_target_properties(ananicy-cpp PROPERTIES LINK_SEARCH_END_STATIC ON)
    set_target_properties(ananicy-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_options(ananicy-cpp PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

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
        find_package(fmt 8.0 REQUIRED)
    else()
        FetchContent_Declare(fmtlib
                GIT_REPOSITORY https://github.com/fmtlib/fmt
                GIT_TAG 8.0.1)

        FetchContent_MakeAvailable(fmtlib)
    endif()
    target_link_libraries(ananicy-cpp PRIVATE fmt::fmt-header-only)
    target_include_directories(ananicy-cpp SYSTEM PRIVATE polyfills/format)
    target_sources(ananicy-cpp PRIVATE polyfills/format/format)
    set(CMAKE_MESSAGE_CONTEXT "")
endif()


# spdlog
message(STATUS "Configuring spdlog")
set(CMAKE_MESSAGE_CONTEXT "spdlog")
if(USE_EXTERNAL_SPDLOG)
    # Stand-alone build
    find_package(spdlog 1.9 REQUIRED)
else()
    FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog
            GIT_TAG v1.9.1
            )

    if(USE_EXTERNAL_FMTLIB)
        message("Using external fmtlib")
        set(SPDLOG_FMT_EXTERNAL_HO ON CACHE INTERNAL "Make spdlog use external, header-only fmtlib")
    endif()

    FetchContent_MakeAvailable(spdlog)
endif()
target_link_libraries(ananicy-cpp PRIVATE spdlog::spdlog)
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
        RUNTIME DESTINATION bin COMPONENT Runtime)

if(ENABLE_SYSTEMD)
    configure_file(ananicy-cpp.service ananicy-cpp.service @ONLY)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ananicy-cpp.service
        DESTINATION lib/systemd/system/)
endif()