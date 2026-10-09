cmake_minimum_required(VERSION 3.16)

# core 的 sqlite3.c 是 C：LANGUAGES 必须带上 C
project(windows VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.10)


# core 库与第三方依赖
set(CORE_DIR     "${CMAKE_CURRENT_LIST_DIR}/../core")
set(SQLITE_DIR   "${CMAKE_CURRENT_LIST_DIR}/sqlite-amalgamation-3530400")
set(NLOHMANN_DIR "${CMAKE_CURRENT_LIST_DIR}/nlohmann")
set(ASIO_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/asio-1.38.2/include")

# 直接用源码编译
set(CORE_SOURCES
    ${CORE_DIR}/src/directory_manager.cpp
    ${CORE_DIR}/src/file_database.cpp
    ${CORE_DIR}/src/language_manager.cpp
    ${CORE_DIR}/src/sync_basic.cpp
    ${CORE_DIR}/src/sync_client.cpp
    ${CORE_DIR}/src/sync_server.cpp
    ${CORE_DIR}/src/tag_manager.cpp
    ${CORE_DIR}/src/tag_serve.cpp
    ${SQLITE_DIR}/sqlite3.c
)


# 可执行文件：新 UI（QML）+ config_loader
qt_add_executable(TagMeow
    Main.cpp
    config_loader.cpp
    config_loader.h
    log.cpp
    log.h
    ${CORE_SOURCES}
)

set_source_files_properties(Store.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(control/Lang.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(control/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(TagMeow
    URI windows
    SOURCES
        configbridge.h
        configbridge.cpp
    QML_FILES
        Main.qml
        Store.qml
        control/Theme.qml
        control/Lang.qml
        control/Btn.qml
        control/Chip.qml
        control/NavItem.qml
        control/PageHeader.qml
        control/TagRectangle.qml
        control/TagContainer.qml
        control/TagDropArea.qml
        control/PageBrowse.qml
        control/PageDirs.qml
        control/PageTags.qml
        control/PageSync.qml
        control/PageSettings.qml
)

# 窗口图标：svg
set(TAGMEOW_ICON_FILES
    img/circle-question-mark.svg
    img/database-plus.svg
    img/download.svg
    img/file.svg
    img/folder-plus.svg
    img/folder.svg
    img/funnel-plus.svg
    img/funnel-x.svg
    img/funnel.svg
    img/monitor-smartphone.svg
    img/paint-roller.svg
    img/power-off.svg
    img/power.svg
    img/rotate-cw.svg
    img/search.svg
    img/settings-2.svg
    img/settings.svg
    img/tag.svg
    img/tagmeow.svg
    img/trash.svg
    img/unplug.svg
)

foreach(icon IN LISTS TAGMEOW_ICON_FILES)
    get_filename_component(icon_name "${icon}" NAME)
    set_source_files_properties("${icon}" PROPERTIES QT_RESOURCE_ALIAS "img/${icon_name}")
endforeach()

qt_add_resources(TagMeow "branding"
    PREFIX "/"
    FILES
        ${TAGMEOW_ICON_FILES}
)

# img/tagmeow.ico(exe 图标
if(WIN32 AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/app.rc")
    target_sources(TagMeow PRIVATE app.rc)
endif()

# 语言文件：LanguageManager 按运行目录的相对路径 ./language 找 *.json 构建后把 language/ 整个拷到可执行文件旁
add_custom_command(TARGET TagMeow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_LIST_DIR}/language"
            "$<TARGET_FILE_DIR:TagMeow>/language"
    COMMENT "Copying language/ next to the executable"
)

target_include_directories(TagMeow PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CORE_DIR}/include
    ${SQLITE_DIR}
    ${NLOHMANN_DIR}
    ${ASIO_INCLUDE}
)

target_compile_definitions(TagMeow PRIVATE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(TagMeow PRIVATE /utf-8)
endif()

set_target_properties(TagMeow PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(TagMeow PRIVATE Qt6::Quick)

if(WIN32)
    target_link_libraries(TagMeow PRIVATE ws2_32 mswsock iphlpapi shell32)
endif()

install(TARGETS TagMeow
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
