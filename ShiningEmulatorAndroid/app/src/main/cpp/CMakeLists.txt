cmake_minimum_required(VERSION 3.10.2)

set(BUILD_SHARED_LIBS SHARED)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../SharedLib ${CMAKE_CURRENT_BINARY_DIR}/lib)

add_library(android-app SHARED
        android_main.cpp
        androidappplatform.cpp
        androidrenderer.cpp
        androidrendererfactory.cpp
        androidresource.cpp)

find_library(log-lib log)

target_link_libraries(android-app
        android
        EGL
        GLESv2
        ${log-lib}
        SharedLib)
