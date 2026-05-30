cmake_minimum_required(VERSION 3.14)
project(tesults VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow vcpkg (or system packages) to provide nlohmann_json; fall back to FetchContent
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(tesults STATIC src/tesults.cpp)
target_include_directories(tesults
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# nlohmann_json is a PRIVATE, header-only dependency used only inside tesults.cpp.
# When found via find_package, link normally. When supplied by FetchContent, use the
# include path directly — avoids exporting a FetchContent target (not in install export set).
if(nlohmann_json_FOUND)
    target_link_libraries(tesults PRIVATE nlohmann_json::nlohmann_json CURL::libcurl OpenSSL::Crypto)
else()
    target_include_directories(tesults PRIVATE "${nlohmann_json_SOURCE_DIR}/include")
    target_link_libraries(tesults PRIVATE CURL::libcurl OpenSSL::Crypto)
endif()

set_target_properties(tesults PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 1
)

# Install targets
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS tesults
    EXPORT tesultsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(TESULTS_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/tesults")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tesultsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tesultsConfig.cmake"
    INSTALL_DESTINATION "${TESULTS_CMAKE_DIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tesultsConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(EXPORT tesultsTargets
    FILE      tesultsTargets.cmake
    NAMESPACE tesults::
    DESTINATION "${TESULTS_CMAKE_DIR}"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tesultsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tesultsConfigVersion.cmake"
    DESTINATION "${TESULTS_CMAKE_DIR}"
)

# Only build examples when tesults is the top-level project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_subdirectory(examples)
endif()
