cmake_minimum_required(VERSION 3.17)
project(ananicy-cpp)

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)
option(USE_EXTERNAL_SPDLOG "Use external Spdlog library" OFF)
option(USE_EXTERNAL_FMTLIB "Use external fmt library" OFF)
option(ENABLE_SYSTEMD "Include systemd headers and install service file" ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MESSAGE_CONTEXT_SHOW ON)

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
        include/worker.hpp)

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
    FetchContent_Declare(json
            GIT_REPOSITORY https://github.com/nlohmann/json
            GIT_TAG v3.9.1)

    FetchContent_MakeAvailable(json)
endif()
target_link_libraries(ananicy-cpp PRIVATE "nlohmann_json::nlohmann_json")
set(CMAKE_MESSAGE_CONTEXT "")

# fmt
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

set(CMAKE_MESSAGE_CONTEXT "")
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

if(ENABLE_SYSTEMD)
    set(CMAKE_MESSAGE_CONTEXT "systemd")
    message(STATUS "Systemd support is enabled, configuring")
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