cmake_minimum_required(VERSION 3.10.2)

set(CMAKE_CXX_STANDARD 17)

add_library(SharedLib ${BUILD_SHARED_LIBS}
        app.cpp
        appplatform.cpp
        appstate.cpp
        font.cpp
        ../lib/lodepng/lodepng.cpp
        ../lib/libxbr-standalone/xbr.cpp
        menu.cpp
        renderer.cpp
        resource.cpp
        shader.cpp
        thread.cpp
        uielements.cpp
        gbcapp/gbcapp.cpp
        gbc/inputset.cpp
        gbcapp/gbcappstate.cpp
        gbcapp/gbcrenderer.cpp
        gbc/gbc.cpp
        gbc/debugwindowmodule.cpp
        gbc/debugutils.cpp
        gbc/framemanager.cpp
        gbc/sgbmodule.cpp
        gbc/sram.cpp
        renderconfig.cpp
        gbcapp/gbcui.cpp gbc/audiounit.cpp gbc/audiounit.h)

target_include_directories(SharedLib PRIVATE ../lib)
