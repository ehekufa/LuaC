#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/.tmp/https
port_file="$PWD/tests/.tmp/https/port"
rm -f "$port_file"
"${PYTHON:-python3}" tests/https_server.py >"$port_file" &
server=$!
trap 'kill "$server" 2>/dev/null || true; wait "$server" 2>/dev/null || true' EXIT
for _ in $(seq 1 100); do
    [[ -s "$port_file" ]] && break
    kill -0 "$server"
    sleep 0.05
done
[[ -s "$port_file" ]]
port=$(head -n1 "$port_file")
export LUAC_HTTPS_TEST_PORT="$port"
export LUAC_HTTPS_TEST_CA="$PWD/tests/.tmp/https/certificate.pem"
"${LUAC:-./luac}" tests/https.lua
"${LUAC:-./luac}" --aot tests/https.lua -o tests/.tmp/https-aot >/dev/null
tests/.tmp/https-aot
