#!/bin/sh

sudo apt-get install -qqy cmake libudev-dev libx11-dev libx11-xcb-dev libxcb1-dev \
    libxrender-dev libxi-dev libxfixes-dev libxext-dev libxkbcommon-dev libglib2.0-dev \
    libgtk2.0-dev libfontconfig1-dev libfreetype6-dev libice-dev libsm-dev libdbus-1-dev \
    libjpeg-dev zlib1g-dev

wget https://bintray.com/artifact/download/koromix/ty/qtbase-$QT_VERSION-x86_64-linux-gcc-static.txz
tar xJf qtbase-$QT_VERSION-x86_64-linux-gcc-static.txz
