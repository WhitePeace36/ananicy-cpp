cmake_minimum_required(VERSION 3.17)
project(ananicy-cpp)

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)
option(USE_EXTERNAL_SPDLOG "Use external Spdlog library" OFF)
option(USE_EXTERNAL_FMTLIB "Use external fmt library" OFF)
option(ENABLE_SYSTEMD "Include systemd headers and install service file" ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto")
set(CMAKE_CXX_STANDARD 20)


add_executable(ananicy-cpp src/main.cpp src/utils.cpp include/utils.hpp src/os/linux/process.cpp include/process.hpp src/rules.cpp include/rules.hpp src/worker.cpp include/worker.hpp include/priority.hpp src/os/linux/priority.cpp include/config.hpp src/config.cpp)
target_link_libraries(ananicy-cpp PRIVATE "pthread")

include(FetchContent)

# nlohmann_json
message(STATUS "Configuring nlohmann_json")
if (USE_EXTERNAL_JSON)
    find_package(nlohmann_json 3.9 REQUIRED)
else()
    FetchContent_Declare(json
            GIT_REPOSITORY https://github.com/nlohmann/json
            GIT_TAG v3.9.1)

    FetchContent_GetProperties(json)
    if(NOT json_POPULATED)
        FetchContent_Populate(json)
        add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
target_link_libraries(ananicy-cpp PRIVATE "nlohmann_json::nlohmann_json")

# fmt
message(STATUS "Configuring fmtlib")
if(USE_EXTERNAL_FMTLIB)
    find_package(fmt)
else()
    FetchContent_Declare(fmtlib
            GIT_REPOSITORY https://github.com/fmtlib/fmt
            GIT_TAG 7.1.3)
    FetchContent_GetProperties(fmtlib)
    if(NOT fmtlib_POPULATED)
        FetchContent_Populate(fmtlib)
        add_subdirectory(${fmtlib_SOURCE_DIR} ${fmtlib_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
target_link_libraries(ananicy-cpp PRIVATE fmt)

# spdlog
message(STATUS "Configuring spdlog")
if(USE_EXTERNAL_SPDLOG)
    # Stand-alone build
    find_package(spdlog 1.8 REQUIRED)
else()
    FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog
            GIT_TAG v1.8.5)

    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        FetchContent_Populate(spdlog)
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()
target_link_libraries(ananicy-cpp PRIVATE spdlog::spdlog)

if(ENABLE_SYSTEMD)
    message(STATUS "Systemd support is enabled, configuring")
    add_compile_definitions(ENABLE_SYSTEMD=1)
    target_link_libraries(ananicy-cpp PRIVATE "systemd")

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