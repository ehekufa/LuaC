#!/usr/bin/env bash
set -euo pipefail
work=$(mktemp -d)
pid=
cleanup() { if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$work"; }
trap cleanup EXIT
failure() { local status=$?; echo "Projects smoke failed at line $1"; cat "$work/studio.log" || true; exit "$status"; }
trap 'failure "$LINENO"' ERR
HOME="$work" LUAC_UI_TRACE=1 ./build/luac-gui > "$work/studio.log" 2>&1 &
pid=$!
window=$(timeout 20s xdotool search --sync --onlyvisible --name 'LuaC Studio' | head -1)
xdotool windowfocus --sync "$window"
sleep 2
click() { xdotool mousemove --window "$window" "$1" "$2" mousedown 1; sleep .3; xdotool mouseup 1; sleep .8; }
click 900 95
grep -q 'dialog=1' "$work/studio.log"
click 600 335
xdotool type --clearmodifiers 'TestProject'
sleep .5
click 760 410
test -f "$work/LuaC/projects/TestProject/main.lua"
grep -q 'page=files' "$work/studio.log"
click 530 95
grep -q 'dialog=2' "$work/studio.log"
click 600 335
xdotool type --clearmodifiers 'second.lua'
sleep .5
click 760 410
test -f "$work/LuaC/projects/TestProject/second.lua"
grep -q 'page=editor' "$work/studio.log"
click 80 140
xdotool key --clearmodifiers ctrl+a
xdotool type --clearmodifiers 'print(123)'
sleep .5
xdotool key --clearmodifiers ctrl+s
sleep 1
grep -q 'print(123)' "$work/LuaC/projects/TestProject/second.lua"
grep -q "Hello from LuaC" "$work/LuaC/projects/TestProject/main.lua"
kill -0 "$pid"
echo 'Vulkan Studio project navigation and multi-file editing: OK'
