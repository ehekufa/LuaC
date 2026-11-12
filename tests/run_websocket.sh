#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/.tmp/websocket
"${PYTHON:-python3}" tests/websocket_test.py --secure
"${LUAC:-./luac}" --aot tests/websocket_secure_client.lua -o tests/.tmp/websocket-secure-client-aot >/dev/null
LUAC_EXECUTABLE="$PWD/tests/.tmp/websocket-secure-client-aot" "${PYTHON:-python3}" tests/websocket_test.py --secure --client-only
