#!/usr/bin/env python3
"""Semantic checks also used by --check, not just code generation."""
import pathlib
import subprocess
import tempfile

cases = [
    (True, 'local x <const> = 1; do local x = x; x = 2 end'),
    (True, 'local x <const> = 1; local function f(x) x = 2 end'),
    (True, 'local x <const> = {}; x.value = 2'),
    (True, 'local x <const> = 1; local x = function() return x end; x = nil'),
    (True, 'local x <const> = 1; for x = 1,2 do x = 2 end'),
    (False, 'local x <const> = 1; x = 2'),
    (False, 'local x <const> = 1; local function f() x = 2 end'),
    (False, 'local x <const> = 1; function x() end'),
    (False, 'local x, y <const> = 1,2; x,y = 3,4'),
    (False, 'repeat local x <const> = 1 until (function() x = 2; return true end)()'),
    (False, 'local x <unknown> = 1'),
    (False, 'local x <close> = nil'),
]
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / 'case.lua'
    for valid, source in cases:
        path.write_text(source, encoding='utf-8')
        result = subprocess.run(['./luac', '--check', str(path)], capture_output=True, text=True)
        assert (result.returncode == 0) == valid, (source, result.stdout, result.stderr)
print('const semantic checks: OK')
