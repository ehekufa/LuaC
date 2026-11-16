#!/usr/bin/env bash
set -euo pipefail
./build/vulkan-orientation > /tmp/luac-vulkan-orientation.log 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
window=$(timeout 15s xdotool search --sync --onlyvisible --name 'LuaC orientation test' | head -1)
sleep 2
kill -0 "$pid" || { cat /tmp/luac-vulkan-orientation.log; exit 1; }
python3 tests/x11_orientation.py "$window"
