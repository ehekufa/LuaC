#!/usr/bin/env bash
set -euo pipefail
work=$(mktemp -d)
pid=
cleanup() { if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$work"; }
trap cleanup EXIT
printf 'original' > "$work/source.lua"
printf '%s' 'Привет, Україна, Беларусь, қазақша — café' > "$work/expected"
./build/luac-gui "$work/source.lua" > "$work/studio.log" 2>&1 &
pid=$!
window=$(timeout 20s xdotool search --sync --onlyvisible --name 'LuaC Studio' | head -1)
sleep 2
kill -0 "$pid" || { cat "$work/studio.log"; exit 1; }
# Open Settings, choose Russian, close it, then edit via the X11 clipboard.
xdotool mousemove --window "$window" 925 36 click 1
sleep 0.5
xdotool mousemove --window "$window" 1172 145 click 1
sleep 0.5
xdotool mousemove --window "$window" 1115 183 click 1
sleep 0.5
xclip -selection clipboard -i "$work/expected"
xdotool mousemove --window "$window" 80 130 click 1
xdotool key --clearmodifiers ctrl+a
sleep 0.3
xdotool key --clearmodifiers ctrl+v
sleep 0.3
xdotool key --clearmodifiers ctrl+a
sleep 0.3
xdotool key --clearmodifiers ctrl+c
sleep 0.3
xclip -selection clipboard -o > "$work/copied"
cmp "$work/expected" "$work/copied"
xdotool key --clearmodifiers ctrl+s
sleep 1
kill -0 "$pid" || { cat "$work/studio.log"; exit 1; }
cmp "$work/expected" "$work/source.lua"
echo 'Vulkan Studio X11 UTF-8 clipboard smoke: OK'
