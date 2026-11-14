#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${TCC:?Set TCC to the TinyCC executable}"
mkdir -p tests/.tmp/tinycc
for name in features compat runtime libraries game pack const warn; do
    echo "TinyCC JIT: $name"
    ./luac --cc "$TCC" "tests/$name.lua" > "tests/.tmp/tinycc/$name-jit.out"
    echo "TinyCC AOT: $name"
    ./luac --cc "$TCC" --aot "tests/$name.lua" -o "tests/.tmp/tinycc/$name" >/dev/null
    "tests/.tmp/tinycc/$name" > "tests/.tmp/tinycc/$name-aot.out"
    cmp "tests/.tmp/tinycc/$name-jit.out" "tests/.tmp/tinycc/$name-aot.out"
done
./luac --emit-c tests/features.lua -o tests/.tmp/tinycc/generated.c >/dev/null
"$TCC" tests/.tmp/tinycc/generated.c -o tests/.tmp/tinycc/generated -lm -ldl
tests/.tmp/tinycc/generated | grep -F 'LuaC tests: OK'
echo 'TinyCC JIT, AOT and emitted C: OK'
