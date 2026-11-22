# Compilation croisée vers Windows 64 bits depuis Linux avec MinGW-w64.
#
#   sudo apt install g++-mingw-w64-x86-64-posix
#   cmake -S . -B build/windows -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#       -DMEX_VENDORED_SDL=ON -DMEX_STATIC_RUNTIME=ON
#   cmake --build build/windows
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# La variante « posix » fournit std::thread ; à défaut, le compilateur générique.
find_program(MEX_MINGW_CC NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(MEX_MINGW_CXX NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
find_program(MEX_MINGW_RC NAMES x86_64-w64-mingw32-windres REQUIRED)
set(CMAKE_C_COMPILER ${MEX_MINGW_CC})
set(CMAKE_CXX_COMPILER ${MEX_MINGW_CXX})
set(CMAKE_RC_COMPILER ${MEX_MINGW_RC})

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
