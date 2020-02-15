cmake_minimum_required(VERSION 3.10.2)

set(BUILD_SHARED_LIBS SHARED)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../SharedLib ${CMAKE_CURRENT_BINARY_DIR}/lib)

set (OBOE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/oboe)

add_library(android-app SHARED
        android_main.cpp
        androidappplatform.cpp
        androidrenderer.cpp
        androidresource.cpp
        androidaudiostreamer.cpp)

find_library(log-lib log)

add_subdirectory(${OBOE_DIR} ./libs/oboe)
include_directories(${OBOE_DIR}/include)

target_link_libraries(android-app
        android
        EGL
        GLESv2
        ${log-lib}
        oboe
        SharedLib)
