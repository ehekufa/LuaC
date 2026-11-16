#!/usr/bin/env python3
"""Check that the sample, demo and both Android launchers use the same PNG."""
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib

root = Path(__file__).resolve().parents[2]
paths = [
    root / "tests/embedded_project/assets/luac_logo.png",
    root / "android/app/src/main/res/drawable-nodpi/luac_logo.png",
    root / "android/app/src/main/assets/samples/love/assets/luac_logo.png",
    root / "android/runner/src/main/res/drawable-nodpi/luac_logo.png",
]
image = paths[0].read_bytes()
assert image[:8] == b"\x89PNG\r\n\x1a\n"
assert struct.unpack(">II", image[16:24]) == (512, 512)
assert len(image) < 1024 * 1024  # SampleCatalog bounds each packaged file.
pos = 8
while pos < len(image):
    length = struct.unpack_from(">I", image, pos)[0]
    chunk = image[pos + 4:pos + 8 + length]
    assert len(chunk) == length + 4
    assert zlib.crc32(chunk) == struct.unpack_from(">I", image, pos + 8 + length)[0]
    pos += length + 12
    if chunk[:4] == b"IEND":
        break
assert pos == len(image)
for path in paths[1:]:
    assert path.read_bytes() == image, path
android = "{http://schemas.android.com/apk/res/android}"
for path in ("android/app/src/main/AndroidManifest.xml", "android/runner/src/main/AndroidManifest.xml"):
    application = ET.parse(root / path).getroot().find("application")
    assert application is not None
    assert application.get(android + "icon") == "@drawable/luac_logo"
    assert application.get(android + "roundIcon") == "@drawable/luac_logo"
print("LuaC logo assets and launcher references: OK")
