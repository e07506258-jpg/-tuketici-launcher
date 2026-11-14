cmake_minimum_required(VERSION 3.21)

project(TitanLauncherCpp
    VERSION 1.0
    LANGUAGES CXX
)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Widgets
    Network
    Concurrent
)

find_package(Qt6ZlibPrivate REQUIRED CONFIG)

add_executable(TitanLauncherCpp
    src/main.cpp
    src/launcher_engine.cpp
    src/zip_archive.cpp
    app_resources.qrc
)

set_source_files_properties(app.ico PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(TitanLauncherCpp PRIVATE app.ico)

if(WIN32)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
        message(FATAL_ERROR "Windows application icon not found: app.ico")
    endif()

    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app.ico" APP_ICON_PATH)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
        @ONLY
    )
    target_sources(TitanLauncherCpp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
endif()

target_include_directories(TitanLauncherCpp PRIVATE
    src
)

target_link_libraries(TitanLauncherCpp PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::ZlibPrivate
)

if(WIN32)
    set_target_properties(TitanLauncherCpp PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    target_link_libraries(TitanLauncherCpp PRIVATE
        Crypt32
        Advapi32
    )

    target_compile_definitions(TitanLauncherCpp PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()
