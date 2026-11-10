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

make libraries >/dev/null
cc -std=c99 -Isrc tests/api_test.c build/libluac.a -o tests/.tmp/api-test
tests/.tmp/api-test
./build/ui-test

./luac --emit-c examples/demo.lua -o tests/.tmp/demo.c >/dev/null
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
fi

echo 'JIT, AOT, C generation and diagnostics: OK'
