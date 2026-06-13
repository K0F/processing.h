#!/bin/bash
export XKB_CONFIG_ROOT=$PREFIX/share/X11/xkb
rm -rf $PREFIX/share/X11/xkb 2>/dev/null
ln -s $PREFIX/share/xkeyboard-config-2 $PREFIX/share/X11/xkb 2>/dev/null
# ----------------------------------

pkill -f termux-x11
pkill -f openbox
termux-x11 :1 &
sleep 1.5

export DISPLAY=:1

openbox &
sleep 0.5

./sketch
