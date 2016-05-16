#!/bin/sh

sudo apt-get install -qqy imagemagick

APPIMAGE_URL_ROOT=https://github.com/probonopd/AppImageKit/releases/download/5/

mkdir AppDir && cd AppDir

wget $APPIMAGE_URL_ROOT/AppRun
chmod +x AppRun
cp ../tyqt/tyqt_linux.desktop tyqt.desktop
mkdir -p usr/bin
install -m755 ../tyqt/tyqt ../tyc/tyc usr/bin/

for size in 16 32 48 256; do
    mkdir -p "usr/share/icons/hicolor/${size}x${size}/apps"
    convert -resize "${size}x${size}" ../../tyqt/images/tyqt.png \
        "usr/share/icons/hicolor/${size}x${size}/apps/tyqt.png"
done

cd ..

VERSION=$(git describe --tags | cut -c2-)
MACHINE=$(uname -m)
if [ "$MACHINE" = "x86_64" ]; then
    MACHINE=64
else
    MACHINE=32
fi

wget $APPIMAGE_URL_ROOT/AppImageAssistant
chmod +x AppImageAssistant
./AppImageAssistant AppDir TyQt-$VERSION-linux$MACHINE.appimage
