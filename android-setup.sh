#!/bin/bash
# android-setup — one-shot provisioning for building/ running sketches on
# Android/Termux. Run this once on the phone inside a Termux session, then use
# ./runAndroid.sh to build + run a sketch.
#
# What it gives you:
#   - x11-repo + termux-x11-nightly : X server backend for the GLFW window
#   - openbox                        : window manager the sketch runs under
#   - freetype, raylib               : on-device libraries pdedeps links against
#   - git (first time only)          : to fetch the repo if you don't have it
#
# Note: Termux:X11 is an Android app — install that APK separately.
set -u

pkg update -y || { echo "pkg update failed" >&2; exit 1; }
pkg install -y x11-repo || { echo "failed to enable x11-repo" >&2; exit 1; }
pkg install -y termux-x11-nightly openbox freetype raylib git \
  || { echo "failed to install packages" >&2; exit 1; }

echo "----------------------------------------------"
echo "Ready. Next:"
echo "  1. Install the Termux:X11 app (APK) and open it."
echo "  2. In Termux: git clone <this repo> && cd <repo>"
echo "  3. Run:  ./runAndroid.sh   (or  ./runAndroid.sh path/to/sketch)"
echo "----------------------------------------------"
