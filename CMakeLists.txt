cmake_minimum_required(VERSION 3.14)
project(tesults VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(tesults STATIC src/tesults.cpp)
target_include_directories(tesults
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(tesults
    PRIVATE
        nlohmann_json::nlohmann_json
        CURL::libcurl
        OpenSSL::Crypto
)
set_target_properties(tesults PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 1
)

# Only build examples when tesults is the top-level project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_subdirectory(examples)
endif()
