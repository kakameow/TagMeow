cmake_minimum_required(VERSION 3.16)
project(TagMeow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4 /wd4100)

    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach()
endif()

find_package(nlohmann_json REQUIRED)

set(SQLITE3_STATIC_LIB "C:/code/msys64/ucrt64/lib/libsqlite3.a" CACHE FILEPATH "Static SQLite3 library")

if(NOT EXISTS "${SQLITE3_STATIC_LIB}")
    message(FATAL_ERROR "找不到静态 SQLite3 库: ${SQLITE3_STATIC_LIB}\n请确认 MSYS2 安装了 mingw-w64-ucrt-x86_64-sqlite3 包")
endif()

add_executable(tagmeow
    cli/main.cpp
    core/src/directory_manager.cpp
    core/src/tag_manager.cpp
    core/src/file_database.cpp
    core/src/tag_serve.cpp
)

# ====== target 创建后才能设置链接选项 ======
if(MINGW)
    target_link_options(tagmeow PRIVATE -static)
endif()

target_include_directories(tagmeow PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(tagmeow PRIVATE
    nlohmann_json::nlohmann_json
    "${SQLITE3_STATIC_LIB}"
)

if(WIN32)
    target_compile_definitions(tagmeow PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        SQLITE_OMIT_LOAD_EXTENSION
    )
    target_link_libraries(tagmeow PRIVATE ws2_32 mswsock iphlpapi)
endif()