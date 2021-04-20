cmake_minimum_required(VERSION 3.17)
project(ananicy_cpp)

option(USE_EXTERNAL_JSON "Use an external JSON library" OFF)

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

if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()


add_executable(ananicy_cpp main.cpp src/utils.cpp include/utils.hpp src/os/linux/process_listener.cpp include/process_listener.hpp src/rules.cpp include/rules.hpp src/worker.cpp include/worker.hpp include/priority.hpp src/os/linux/priority.cpp)

target_link_libraries(ananicy_cpp PRIVATE "pthread" "nlohmann_json::nlohmann_json")
target_link_libraries(ananicy_cpp PRIVATE spdlog::spdlog_header_only)

target_include_directories(ananicy_cpp PRIVATE "${CMAKE_HOME_DIRECTORY}/include")