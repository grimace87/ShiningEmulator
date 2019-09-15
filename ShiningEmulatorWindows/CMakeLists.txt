cmake_minimum_required(VERSION 3.14)
project(ShiningEmulatorWindows)

set(CMAKE_CXX_STANDARD 17)

ADD_DEFINITIONS(-DUNICODE)

set(BUILD_SHARED_LIBS STATIC)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SharedLib ${CMAKE_CURRENT_BINARY_DIR}/lib)

# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lib/libxbr-standalone ${CMAKE_CURRENT_BINARY_DIR}/lib/xbr)

add_executable(ShiningEmulatorWindows WIN32
        res/res.rc
        windowsappplatform.cpp
        windowsfilehelper.cpp
        windowsmain.cpp
        windowsrenderer.cpp
        windowsrendererfactory.cpp
        windowsresource.cpp)

target_include_directories(ShiningEmulatorWindows PRIVATE ../lib)

target_link_libraries(ShiningEmulatorWindows SharedLib OpenGL32 xinput)
