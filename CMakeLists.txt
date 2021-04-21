cmake_minimum_required(VERSION 3.17)
project(ananicy-cpp)

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)
option(USE_EXTERNAL_SPDLOG "Use external Spdlog library" OFF)
option(ENABLE_SYSTEMD "Include systemd headers and install service file" ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto")
set(CMAKE_CXX_STANDARD 20)


if (USE_EXTERNAL_JSON)
    find_package(nlohmann_json 3.9.1 REQUIRED)
else()
    include(FetchContent)

    FetchContent_Declare(json
            GIT_REPOSITORY https://github.com/nlohmann/json
            GIT_TAG v3.9.1)

    FetchContent_GetProperties(json)
    if(NOT json_POPULATED)
        FetchContent_Populate(json)
        add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()

if(USE_EXTERNAL_SPDLOG)
    # Stand-alone build
    find_package(spdlog REQUIRED)
else()
    include(FetchContent)

    FetchContent_Declare(spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog
            GIT_TAG v1.8.5)

    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        FetchContent_Populate(spdlog)
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()


add_executable(ananicy-cpp src/main.cpp src/utils.cpp include/utils.hpp src/os/linux/process.cpp include/process.hpp src/rules.cpp include/rules.hpp src/worker.cpp include/worker.hpp include/priority.hpp src/os/linux/priority.cpp include/config.hpp src/config.cpp)

target_link_libraries(ananicy-cpp PRIVATE "pthread" "nlohmann_json::nlohmann_json")
target_link_libraries(ananicy-cpp PRIVATE spdlog::spdlog_header_only)

if(ENABLE_SYSTEMD)
    message(STATUS "Enabling systemd")
    add_compile_definitions(ENABLE_SYSTEMD=1)
    target_link_libraries(ananicy-cpp PRIVATE "systemd")

endif()

find_package(fmt)
target_link_libraries(ananicy-cpp PRIVATE fmt)

target_include_directories(ananicy-cpp PRIVATE "${CMAKE_HOME_DIRECTORY}/include")

include(GNUInstallDirs)
install(TARGETS ananicy-cpp
        RUNTIME DESTINATION bin)

if(ENABLE_SYSTEMD)
    configure_file(ananicy-cpp.service ananicy-cpp.service @ONLY)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ananicy-cpp.service
        DESTINATION lib/systemd/system/)
endif()