# Compilation on Windows

## MSVC 2015 32-bit with static MSVCRT

Download qtbase source from http://download.qt.io/official_releases/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.7z

Extract the directory inside it to "ty/qt5" and rename "qtbase-opensource-src-5.6.1" to
"i686-win32-msvc2015-mt". Open the "VS2015 x86 Native Tools Command Prompt" and cd to
this directory.

```batch
cd i686-win32-msvc2015-mt
REM Now we are in ty/qt5/i686-win32-msvc2015-mt
configure -platform win32-msvc2015 ^
    -opensource ^
    -confirm-license ^
    -static ^
    -static-runtime ^
    -release ^
    -nomake examples ^
    -nomake tests ^
    -no-opengl ^
    -no-harfbuzz ^
    -no-icu ^
    -no-cups ^
    -qt-pcre ^
    -qt-zlib ^
    -qt-freetype ^
    -qt-libpng ^
    -qt-libjpeg
nmake
```

Unfortunately Qt static builds are fragile and cannot be moved around. You will need to rebuild Qt
if you move your project.

## MSVC 2015 64-bit with static MSVCRT

Download qtbase source from http://download.qt.io/official_releases/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.7z

Extract the directory inside it to "ty/qt5" and rename "qtbase-opensource-src-5.6.1" to
"x86_64-win32-msvc2015-mt". Open the "VS2015 x64 Native Tools Command Prompt" and cd to
this directory.

```batch
cd x86_64-win32-msvc2015-mt
REM Now we are in ty/qt5/x86_64-win32-msvc2015-mt
configure -platform win32-msvc2015 ^
    -opensource ^
    -confirm-license ^
    -static ^
    -static-runtime ^
    -release ^
    -nomake examples ^
    -nomake tests ^
    -no-opengl ^
    -no-harfbuzz ^
    -no-icu ^
    -no-cups ^
    -qt-pcre ^
    -qt-zlib ^
    -qt-freetype ^
    -qt-libpng ^
    -qt-libjpeg
nmake
```

Unfortunately Qt static builds are fragile and cannot be moved around. You will need to rebuild Qt
if you move your project.

# Mac OS X / Clang 64-bit

A recent version of XCode must be installed.

Download qtbase source from http://download.qt.io/official_releases/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.tar.xz

Extract the directory inside it to "ty/qt5" and rename "qtbase-opensource-src-5.6.1" to
"x86_64-darwin-clang". Open a command prompt and go to that directory.

```sh
cd x86_64-darwin-clang
# Now we are in ty/qt5/x86_64-darwin-clang
./configure -platform macx-clang \
    -opensource \
    -confirm-license \
    -static \
    -release \
    -nomake examples \
    -nomake tests \
    -no-pkg-config \
    -no-harfbuzz \
    -no-icu \
    -no-cups \
    -no-freetype \
    -qt-pcre
make
```

Unfortunately Qt static builds are fragile and cannot be moved around. You will need to rebuild Qt
if you move your project.

# Cross-compiling for Windows from Linux with MinGW-w64

## 32-bit build with MinGW-w64

Download qtbase source from http://download.qt.io/official_releases/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.tar.xz

Extract the directory inside it to "ty/qt5" and rename "qtbase-opensource-src-5.6.1" to
"i686-w64-mingw32". Open a terminal and go to that directory.

```sh
cd i686-w64-mingw32
# Now we are in ty/qt5/i686-w64-mingw32
./configure -xplatform win32-g++ \
    -device-option CROSS_COMPILE=i686-w64-mingw32- \
    -opensource \
    -confirm-license \
    -static \
    -release \
    -nomake examples \
    -nomake tests \
    -no-pkg-config \
    -no-opengl \
    -no-harfbuzz \
    -no-icu \
    -no-cups \
    -qt-pcre \
    -qt-zlib \
    -qt-freetype \
    -qt-libpng \
    -qt-libjpeg
make
```

Unfortunately Qt static builds are fragile and cannot be moved around. You will need to rebuild Qt
if you move your project.

## 64-bit build with MinGW-w64

Download qtbase source from http://download.qt.io/official_releases/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.tar.xz

Extract the directory inside it to "ty/qt5" and rename "qtbase-opensource-src-5.6.1" to
"x86_64-w64-mingw32". Open a terminal and go to that directory.

```sh
cd x86_64-w64-mingw32
# Now we are in ty/qt5/x86_64-w64-mingw32
./configure -xplatform win32-g++ \
    -device-option CROSS_COMPILE=x86_64-w64-mingw32- \
    -opensource \
    -confirm-license \
    -static \
    -release \
    -nomake examples \
    -nomake tests \
    -no-pkg-config \
    -no-opengl \
    -no-harfbuzz \
    -no-icu \
    -no-cups \
    -qt-pcre \
    -qt-zlib \
    -qt-freetype \
    -qt-libpng \
    -qt-libjpeg
make
```

Unfortunately Qt static builds are fragile and cannot be moved around. You will need to rebuild Qt
if you move your project.
