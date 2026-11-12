#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/.tmp

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
fi

echo 'JIT, AOT, C generation and diagnostics: OK'
