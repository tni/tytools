#!/bin/sh

sudo rm -f /etc/apt/sources.list.d/google-chrome.list
sudo dpkg --add-architecture i386
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update -y

sudo apt-get install -y gcc-multilib g++-multilib
sudo apt-get install -y \
    gir1.2-freedesktop:i386 \
    gir1.2-gdkpixbuf-2.0:i386 \
    gir1.2-glib-2.0:i386 \
    libcairo2-dev:i386 \
    libgdk-pixbuf2.0-dev:i386 \
    libgirepository-1.0-1:i386 \
    libglib2.0-dev:i386 \
    libudev-dev:i386 \
    libx11-dev:i386 \
    libx11-xcb-dev:i386 \
    libxcb1-dev:i386 \
    libxrender-dev:i386 \
    libxi-dev:i386 \
    libxfixes-dev:i386 \
    libxext-dev:i386 \
    libxkbcommon-dev:i386 \
    libglib2.0-dev:i386 \
    libgtk2.0-dev:i386 \
    libfontconfig1-dev:i386 \
    libfreetype6-dev:i386 \
    libice-dev:i386 \
    libsm-dev:i386 \
    libdbus-1-dev:i386 \
    libjpeg-dev:i386 \
    zlib1g-dev:i386

wget https://cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz
tar xzf cmake-3.5.2-Linux-x86_64.tar.gz
export PATH=$PWD/cmake-3.5.2-Linux-x86_64/bin:$PATH

wget https://bintray.com/artifact/download/koromix/ty/qtbase-$QT_VERSION-i686-linux-gcc-static.txz
tar xJf qtbase-$QT_VERSION-i686-linux-gcc-static.txz
