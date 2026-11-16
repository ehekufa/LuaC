#!/usr/bin/env python3
"""Audit the actual published APK, including native code for phone ABIs."""
import struct
import sys
import zipfile

apk = sys.argv[1]
abis = {"armeabi-v7a": (1, 40), "arm64-v8a": (2, 183), "x86": (1, 3), "x86_64": (2, 62)}
with zipfile.ZipFile(apk) as archive:
    corrupt = archive.testzip()
    if corrupt:
        raise SystemExit("Corrupt ZIP entry: " + corrupt)
    names = set(archive.namelist())
    for required in ("AndroidManifest.xml", "resources.arsc", "classes.dex"):
        if required not in names:
            raise SystemExit("Missing APK entry: " + required)
    for abi, (elf_class, machine) in abis.items():
        path = "lib/" + abi + "/libluac_native.so"
        if path not in names:
            raise SystemExit("Missing native library: " + path)
        info = archive.getinfo(path)
        if info.compress_type == zipfile.ZIP_STORED:
            archive.fp.seek(info.header_offset)
            header = archive.fp.read(30)
            if header[:4] != b"PK\x03\x04":
                raise SystemExit("Invalid ZIP local header: " + path)
            name_length, extra_length = struct.unpack_from("<HH", header, 26)
            data_offset = info.header_offset + 30 + name_length + extra_length
            if data_offset % 16384:
                raise SystemExit("Uncompressed native library is not 16 KiB ZIP-aligned: " + path)
        data = archive.read(path)
        if data[:6] != b"\x7fELF" + bytes([elf_class, 1]) or struct.unpack_from("<H", data, 18)[0] != machine:
            raise SystemExit("Wrong native architecture: " + path)
        if elf_class == 1:
            phoff = struct.unpack_from("<I", data, 28)[0]
            entsize, count = struct.unpack_from("<HH", data, 42)
            fmt, offset_pos, vaddr_pos, align_pos = "<I", 4, 8, 28
        else:
            phoff = struct.unpack_from("<Q", data, 32)[0]
            entsize, count = struct.unpack_from("<HH", data, 54)
            fmt, offset_pos, vaddr_pos, align_pos = "<Q", 8, 16, 48
        loads = 0
        for i in range(count):
            base = phoff + entsize * i
            if struct.unpack_from("<I", data, base)[0] != 1:
                continue
            offset = struct.unpack_from(fmt, data, base + offset_pos)[0]
            address = struct.unpack_from(fmt, data, base + vaddr_pos)[0]
            alignment = struct.unpack_from(fmt, data, base + align_pos)[0]
            if alignment < 16384 or (address - offset) % 16384:
                raise SystemExit("Native LOAD segment is not 16 KiB compatible: " + path)
            loads += 1
        if not loads:
            raise SystemExit("No native LOAD segments: " + path)
        print(abi + ": ELF architecture and 16 KiB segment alignment OK")
print("Signed release APK ZIP and native ABIs: OK")
