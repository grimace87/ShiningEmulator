
## Shining Emulator

Emulates a game boy system. Projects included for Windows and Android.

Does not include any games, and never will include any games that aren't made freely available by the developer.

#### Building

- Windows project based on root-level CMakeLists.txt; tested using CLion and Visual Studio 2019. Open the root directory and
  load the CMakeLists.txt. Requires the MSVC toolchain installed (install Visual Studio with C++ Desktop development components).
- Android project based on root-level build.gradle; tested using Android Studio. Import the project in the root directory.

#### Library dependencies

Included in this repo:

- 'KHR/khrplatform.h' (Windows only) - required by 'glext.h'
- Treeki/libxbr-standalone (modified from original) - upscale pixel graphics using xBR algorithm

Installed using submodules:

- ivandeve/lodepng - lightweight PNG encoder/decoder
- g-truc/glm - OpenGL matrix mathematics library
- KhronosGroup/OpenGL-Registry (Windows only) - for OpenGL bindings from 'glext.h'
- google/oboe (Android only) - native audio library
