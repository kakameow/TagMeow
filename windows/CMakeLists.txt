cmake_minimum_required(VERSION 3.16)

project(windows VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()


set(CORE_DIR     "${CMAKE_CURRENT_LIST_DIR}/../core")
set(SQLITE_DIR   "${CMAKE_CURRENT_LIST_DIR}/sqlite-amalgamation-3530400")
set(NLOHMANN_DIR "${CMAKE_CURRENT_LIST_DIR}/nlohmann")
set(ASIO_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/asio-1.38.2/include")

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

qt_add_executable(TagMeow
    main.cpp
    bridge.cpp
    bridge.h
    config_loader.cpp
    config_loader.h
    ${CORE_SOURCES}
)

target_include_directories(TagMeow PRIVATE
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

qt_add_resources(TagMeow "images"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_LIST_DIR}"
    FILES
        img/settings.svg
        img/tag.svg
        img/file.svg
        img/folder.svg
        img/trash.svg
        img/settings-2.svg
        img/download.svg
        img/rotate-cw.svg
        img/search.svg
        img/circle-question-mark.svg
        img/funnel-x.svg
        img/tag-x.svg
        img/folder-x.svg
        img/funnel-plus.svg
        img/tag-plus.svg
        img/folder-plus.svg
        img/funnel.svg
        img/paint-roller.svg
        img/monitor-smartphone.svg
        img/database-plus.svg
        img/arrow-big-left.svg
        img/power.svg
        img/power-off.svg
        img/unplug.svg
)

qt_add_qml_module(TagMeow
    URI windows
    QML_FILES
        Main.qml
        control/PictureButton.qml
        QML_FILES control/DataDisplayLabel.qml
        QML_FILES control/TagRectangle.qml
        QML_FILES control/TagContainer.qml
        QML_FILES control/LibraryTag.qml
        QML_FILES control/DirContainer.qml
        QML_FILES control/FileContainer.qml
        SOURCES transferdata.h transferdata.cpp
        QML_FILES control/PictureTextFieldButton.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(TagMeow PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appwindows
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(TagMeow
    PRIVATE Qt6::Quick ws2_32 mswsock iphlpapi
)

install(TARGETS TagMeow
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
