cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)

add_library(SharedLib ${BUILD_SHARED_LIBS}
        app.cpp
        appplatform.cpp
        appstate.cpp
        font.cpp
        ../lib/lodepng/lodepng.cpp
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
        gbc/framemanager.cpp
        gbc/sgbmodule.cpp
        gbc/sram.cpp
        renderconfig.cpp)

target_include_directories(SharedLib PRIVATE ../lib)
