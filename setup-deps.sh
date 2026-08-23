#!/bin/sh
# setup-deps — install everything needed to build & test sketches natively.
#
# Installs raylib runtime dev headers (X11/GL/ALSA) via apt, then builds
# raylib from source into /usr/local (Debian ships no usable libraylib-dev).
# Run once; rerun safe.
set -e

sudo apt-get update
sudo apt-get install -y \
  build-essential git make \
  libx11-dev libxcursor-dev libxrandr-dev libxi-dev \
  libxinerama-dev libxfixes-dev \
  libgl1-mesa-dev libasound2-dev

RAYLIB_DIR="$HOME/src/raylib"
if [ ! -d "$RAYLIB_DIR" ]; then
  git clone --depth 1 https://github.com/raysan5/raylib "$RAYLIB_DIR"
fi

make -C "$RAYLIB_DIR/src" PLATFORM=PLATFORM_DESKTOP -j"$(nproc)"
sudo make -C "$RAYLIB_DIR/src" install PLATFORM=PLATFORM_DESKTOP
sudo ldconfig

echo "ok: $(pkg-config --modversion raylib 2>/dev/null || echo raylib) installed"
