#!/usr/bin/env bash
# Assemble the .c4z sources in build/: the shared control4-lua-mqtt framing (the
# global C4MQTT) concatenated ahead of the driver body, since a .c4z has no `require`.
# CI then runs Control4's driver packager on build/ (speakerpoint.c4zproj) to stamp the
# version and produce openspeakerpoint.c4z.
set -euo pipefail
cd "$(dirname "$0")"
lib=control4-lua-mqtt/mqtt.lua
[ -f "$lib" ] || { echo "submodule missing: git submodule update --init --recursive" >&2; exit 1; }
rm -rf build && mkdir build
cat "$lib" driver.src.lua > build/driver.lua
cp driver.xml speakerpoint.c4zproj build/
echo "built build/driver.lua ($(wc -l < build/driver.lua) lines)"
