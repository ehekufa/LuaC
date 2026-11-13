#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/.tmp

./luac tests/warn.lua > tests/.tmp/warn-jit.out 2> tests/.tmp/warn-jit.err
printf '%s\n' 'warn: OK' > tests/.tmp/warn-expected.out
printf '%s\n' 'Lua warning: hello 42' 'Lua warning: @on ordinary' > tests/.tmp/warn-expected.err
cmp tests/.tmp/warn-expected.out tests/.tmp/warn-jit.out
cmp tests/.tmp/warn-expected.err tests/.tmp/warn-jit.err
./luac --aot tests/warn.lua -o tests/.tmp/warn-aot >/dev/null
tests/.tmp/warn-aot > tests/.tmp/warn-aot.out 2> tests/.tmp/warn-aot.err
cmp tests/.tmp/warn-expected.out tests/.tmp/warn-aot.out
cmp tests/.tmp/warn-expected.err tests/.tmp/warn-aot.err

./luac --check tests/features.lua >/dev/null
./luac tests/features.lua > tests/.tmp/jit.out
printf '%s\n' 'LuaC tests: OK' > tests/.tmp/expected.out
cmp tests/.tmp/expected.out tests/.tmp/jit.out

./luac --aot tests/features.lua -o tests/.tmp/features-aot >/dev/null
tests/.tmp/features-aot > tests/.tmp/aot.out
cmp tests/.tmp/expected.out tests/.tmp/aot.out

./luac tests/compat.lua > tests/.tmp/compat-jit.out
printf '%s\n' 'LuaC compatibility: OK' > tests/.tmp/compat-expected.out
cmp tests/.tmp/compat-expected.out tests/.tmp/compat-jit.out
./luac --aot tests/compat.lua -o tests/.tmp/compat-aot >/dev/null
tests/.tmp/compat-aot > tests/.tmp/compat-aot.out
cmp tests/.tmp/compat-expected.out tests/.tmp/compat-aot.out

./luac tests/libraries.lua > tests/.tmp/libraries-jit.out
printf '%s\n' 'LuaC extended libraries: OK' > tests/.tmp/libraries-expected.out
cmp tests/.tmp/libraries-expected.out tests/.tmp/libraries-jit.out
./luac --aot tests/libraries.lua -o tests/.tmp/libraries-aot >/dev/null
tests/.tmp/libraries-aot > tests/.tmp/libraries-aot.out
cmp tests/.tmp/libraries-expected.out tests/.tmp/libraries-aot.out

./luac tests/runtime.lua > tests/.tmp/runtime-jit.out
printf '%s\n' 'LuaC runtime roadmap: OK' > tests/.tmp/runtime-expected.out
cmp tests/.tmp/runtime-expected.out tests/.tmp/runtime-jit.out
./luac --aot tests/runtime.lua -o tests/.tmp/runtime-aot >/dev/null
tests/.tmp/runtime-aot > tests/.tmp/runtime-aot.out
cmp tests/.tmp/runtime-expected.out tests/.tmp/runtime-aot.out
runtime_aot="$(pwd)/tests/.tmp/runtime-aot"
(cd / && "$runtime_aot" > "$OLDPWD/tests/.tmp/runtime-aot-detached.out")
cmp tests/.tmp/runtime-expected.out tests/.tmp/runtime-aot-detached.out

case "$(uname -s 2>/dev/null)" in
  Darwin) ffi_ext=dylib; ffi_shared=-dynamiclib ;;
  MINGW*|MSYS*|CYGWIN*) ffi_ext=dll; ffi_shared=-shared ;;
  *) ffi_ext=so; ffi_shared=-shared ;;
esac
cc -std=c99 $ffi_shared tests/ffi_fixture.c -o "tests/.tmp/ffi-fixture.$ffi_ext"
LUAC_FFI_FIXTURE="tests/.tmp/ffi-fixture.$ffi_ext" ./luac tests/ffi.lua > tests/.tmp/ffi-jit.out
printf '%s\n' 'LuaC FFI: OK' > tests/.tmp/ffi-expected.out
cmp tests/.tmp/ffi-expected.out tests/.tmp/ffi-jit.out
./luac --aot tests/ffi.lua -o tests/.tmp/ffi-aot >/dev/null
LUAC_FFI_FIXTURE="tests/.tmp/ffi-fixture.$ffi_ext" tests/.tmp/ffi-aot > tests/.tmp/ffi-aot.out
cmp tests/.tmp/ffi-expected.out tests/.tmp/ffi-aot.out

python3 tests/websocket_test.py
./luac --aot tests/websocket_client.lua -o tests/.tmp/websocket-client-aot >/dev/null
LUAC_EXECUTABLE="$(pwd)/tests/.tmp/websocket-client-aot" python3 tests/websocket_test.py --client-only

./luac tests/game.lua > tests/.tmp/game-jit.out
printf '%s\n' 'LuaC game assets physics input: OK' > tests/.tmp/game-expected.out
cmp tests/.tmp/game-expected.out tests/.tmp/game-jit.out
./luac --aot tests/game.lua -o tests/.tmp/game-aot >/dev/null
tests/.tmp/game-aot > tests/.tmp/game-aot.out
cmp tests/.tmp/game-expected.out tests/.tmp/game-aot.out

./luac tests/audio.lua > tests/.tmp/audio-jit.out
printf '%s\n' 'LuaC native audio: OK' > tests/.tmp/audio-expected.out
cmp tests/.tmp/audio-expected.out tests/.tmp/audio-jit.out
./luac --aot tests/audio.lua -o tests/.tmp/audio-aot >/dev/null
tests/.tmp/audio-aot > tests/.tmp/audio-aot.out
cmp tests/.tmp/audio-expected.out tests/.tmp/audio-aot.out

make libraries >/dev/null
cc -std=c99 -Isrc tests/api_test.c build/libluac.a -o tests/.tmp/api-test
tests/.tmp/api-test
./build/ui-test
./build/engine-test

./luac --emit-c examples/demo.lua -o tests/.tmp/demo.c >/dev/null
./luac --emit-c tests/compat.lua -o tests/.tmp/compat.c >/dev/null
./luac --emit-c tests/libraries.lua -o tests/.tmp/libraries.c >/dev/null
./luac --emit-c tests/audio.lua -o tests/.tmp/audio.c >/dev/null
./luac --emit-c tests/game.lua -o tests/.tmp/game.c >/dev/null
./luac --emit-c tests/runtime.lua -o tests/.tmp/runtime.c >/dev/null
grep -q 'lc_jit_main' tests/.tmp/demo.c
grep -q 'LC_RELEASE' tests/.tmp/demo.c

if ./luac --check tests/invalid.lua >/dev/null 2>&1; then
  echo 'invalid.lua was accepted' >&2
  exit 1
fi

if [ "${SANITIZE:-0}" = 1 ]; then
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/demo.c -o tests/.tmp/demo-sanitize -lm
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/demo-sanitize >/dev/null
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/compat.c -o tests/.tmp/compat-sanitize -lm
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/compat-sanitize >/dev/null
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/libraries.c -o tests/.tmp/libraries-sanitize -lm
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/libraries-sanitize >/dev/null
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/audio.c -o tests/.tmp/audio-sanitize -lm
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/audio-sanitize >/dev/null
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/runtime.c -o tests/.tmp/runtime-sanitize -lm -ldl
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/runtime-sanitize >/dev/null
  cc -std=gnu99 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DLUAC_STANDALONE \
    tests/.tmp/game.c -o tests/.tmp/game-sanitize -lm -ldl
  ASAN_OPTIONS=detect_leaks=1 tests/.tmp/game-sanitize >/dev/null
fi

echo 'JIT, AOT, C generation and diagnostics: OK'
