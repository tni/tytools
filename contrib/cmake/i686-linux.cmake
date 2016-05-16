set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER cc -m32)
set(CMAKE_CXX_COMPILER c++ -m32)

set(CMAKE_SYSTEM_IGNORE_PATH
    /lib             /lib64             /lib32
    /usr/lib         /usr/lib64         /usr/lib32
    /usr/local/lib   /usr/local/lib64   /usr/local/lib32)
list(APPEND CMAKE_LIBRARY_PATH /usr/local/lib/i386-linux-gnu)
list(APPEND CMAKE_LIBRARY_PATH /usr/lib/i386-linux-gnu)
list(APPEND CMAKE_LIBRARY_PATH /lib/i386-linux-gnu)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH 0)
set(ENV{PKG_CONFIG_PATH} /usr/lib/i386-linux-gnu/pkgconfig)

# Adjust the default behaviour of the FIND_XXX() commands:
#  - search headers and libraries in the target environment
#  - search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
