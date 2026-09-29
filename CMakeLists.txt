cmake_minimum_required(VERSION 3.22)

project(sangeet
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus Sql)
find_package(PkgConfig REQUIRED)

pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(sangeet
    src/main.cpp
    src/mainwindow.h
    src/scanning.cpp
    src/folder_select.cpp
    src/folder_select.h
    src/albums.cpp
    src/albums.h
    src/playlist.cpp
    src/playlist.h
    src/tracks.cpp
    src/tracks.h
    src/player.cpp
    src/player.h
    src/loudness_graph.h
    src/eq_engine.h
    src/eq_window.cpp
    src/eq_window.h
    resources.qrc
)

target_link_libraries(sangeet PRIVATE
    Qt6::Widgets
    Qt6::DBus
    Qt6::Sql
    PkgConfig::TAGLIB
    PkgConfig::MPV
)

target_compile_features(sangeet PRIVATE cxx_std_23)

if (MSVC)
    target_compile_options(sangeet PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
        /Zc:__cplusplus
        /Zc:preprocessor
    )
else()
    target_compile_options(sangeet PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wpointer-arith
        -Wcast-align
        -Wcast-qual
        -Wwrite-strings
        -Wmissing-declarations
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wold-style-cast
        -Wdouble-promotion
        -Wimplicit-fallthrough
        -Wnull-dereference
        -Wvla
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sangeet PRIVATE
        $<$<CONFIG:Debug>:
            -O0
            -g3
            -fno-omit-frame-pointer
        >
        $<$<CONFIG:Release>:
            -O3
            -DNDEBUG
            -fstack-protector-strong
            -fno-omit-frame-pointer
            -ffunction-sections
            -fdata-sections
        >
        $<$<CONFIG:RelWithDebInfo>:
            -O2
            -g
            -DNDEBUG
            -fstack-protector-strong
            -fno-omit-frame-pointer
        >
    )

    target_link_options(sangeet PRIVATE
        $<$<CONFIG:Release>:
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,--gc-sections
        >
        $<$<CONFIG:RelWithDebInfo>:
            -Wl,-z,relro
            -Wl,-z,now
        >
    )
endif()

include(GNUInstallDirs)

install(
    TARGETS sangeet
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    FILES sangeet.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(
    FILES sangeet.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
