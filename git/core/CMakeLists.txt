cmake_minimum_required(VERSION 3.16)
project(tagmeow_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(tagmeow_core STATIC
    src/directory_manager.cpp
    src/language_manager.cpp
    src/tag_manager.cpp
    src/file_database.cpp
    src/tag_serve.cpp
    src/sync_basic.cpp
    src/sync_client.cpp
    src/sync_server.cpp
)

target_include_directories(tagmeow_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tagmeow_core PUBLIC
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)

target_compile_definitions(tagmeow_core PUBLIC
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)

if(WIN32)
    target_compile_definitions(tagmeow_core PUBLIC
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(tagmeow_core PUBLIC ws2_32 mswsock iphlpapi)
endif()
