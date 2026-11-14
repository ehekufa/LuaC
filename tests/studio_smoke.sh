#!/usr/bin/env bash
set -euo pipefail
work=$(mktemp -d)
pid=
cleanup() { if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi; rm -rf "$work"; }
trap cleanup EXIT
report_failure() {
  local status=$? line=$1
  trap - ERR
  echo "Studio smoke failed at line $line (exit $status)"
  cat "$work/studio.log" 2>/dev/null || true
  if [ -f "$work/source.lua" ]; then printf 'Saved text: '; cat "$work/source.lua"; echo; fi
  local message
  message=$( { printf 'log: '; cat "$work/studio.log" 2>/dev/null || true; printf ' copied: '; cat "$work/copied" 2>/dev/null || true; printf ' saved: '; cat "$work/source.lua" 2>/dev/null || true; } | tail -c 3000 | tr '\r\n' '  ' | sed 's/%/%25/g')
  printf '::error file=tests/studio_smoke.sh::Line %s, exit %s: %s\n' "$line" "$status" "$message"
  exit "$status"
}
trap 'report_failure "$LINENO"' ERR
printf 'original' > "$work/source.lua"
printf '%s' 'Привет, Україна, Беларусь, қазақша — café' > "$work/expected"
HOME="$work" LUAC_UI_TRACE=1 ./build/luac-gui "$work/source.lua" > "$work/studio.log" 2>&1 &
pid=$!
window=$(timeout 20s xdotool search --sync --onlyvisible --name 'LuaC Studio' | head -1)
xdotool windowfocus --sync "$window"
sleep 2
kill -0 "$pid" || { cat "$work/studio.log"; exit 1; }
# Keep each press down long enough for a software-rendered frame.
click_at() {
  xdotool mousemove --window "$window" "$1" "$2" mousedown 1
  sleep 0.3
  xdotool mouseup 1
}
# Software Vulkan can render slowly on shared CI runners. Retry only until the
# expected state appears; this avoids toggling Settings a second time.
click_until_trace() {
  local x=$1 y=$2 expected=$3 attempt
  for attempt in 1 2 3 4 5; do
    click_at "$x" "$y"
    sleep 0.7
    if grep -qF "$expected" "$work/studio.log"; then return 0; fi
  done
  echo "UI state not observed: $expected" >&2
  return 1
}
click_until_trace 925 36 'settings=open'
click_until_trace 1172 145 'language=ru'
click_until_trace 1225 332 'theme=changed'
click_at 1115 507
sleep 0.5
test -s "$work/.luac-studio.conf"
xclip -selection clipboard -i "$work/expected"
click_at 80 130
xdotool key --clearmodifiers ctrl+a
sleep 0.3
xdotool key --clearmodifiers ctrl+v
sleep 0.3
xdotool key --clearmodifiers ctrl+a
sleep 0.3
printf 'clipboard sentinel' | xclip -selection clipboard
xdotool key --clearmodifiers ctrl+c
sleep 0.3
xclip -selection clipboard -o > "$work/copied"
cmp "$work/expected" "$work/copied"
xdotool key --clearmodifiers ctrl+s
sleep 1
kill -0 "$pid" || { cat "$work/studio.log"; exit 1; }
cmp "$work/expected" "$work/source.lua"
echo 'Vulkan Studio X11 UTF-8 clipboard smoke: OK'
