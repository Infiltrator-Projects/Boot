#!/usr/bin/env python3
"""Mutate independently located ELF structures; invoke the production parser."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

validator, module = sys.argv[1:]
original = Path(module).read_bytes()
u16 = lambda at: struct.unpack_from("<H", original, at)[0]
u64 = lambda at: struct.unpack_from("<Q", original, at)[0]
sections = [struct.unpack_from("<IIQQQQIIQQ", original, u64(40)+i*u16(58))
            for i in range(u16(60))]
names = sections[u16(62)]
section_name = lambda s: original[names[4]+s[0]:].split(b"\0", 1)[0]
named = {section_name(s): s for s in sections}
manifest = named[b".ifsboot"][4]
rela = next(s for s in sections if s[1] == 4 and s[5])
symbols = next(s for s in sections if s[1] == 11)
programs = [(u64(32)+i*u16(54), struct.unpack_from("<IIQQQQQQ", original, u64(32)+i*u16(54)))
            for i in range(u16(56))]
load = next((at, p) for at, p in programs if p[0] == 1 and p[1] & 1)
cases = {
    "signature": (0, b"NOPE"), "class": (4, b"\x01"),
    "endian": (5, b"\x02"), "machine": (18, struct.pack("<H", 183)),
    "executable": (16, struct.pack("<H", 2)),
    "section-overflow": (40, struct.pack("<Q", 2**64-1)),
    "program-count": (56, struct.pack("<H", 65535)),
    "profile": (manifest+20, struct.pack("<I", 99)),
    "abi": (manifest+12, struct.pack("<I", 99)),
    "format-unterminated": (manifest+24, b"x"*16),
    "writable-code": (load[0]+4, struct.pack("<I", 7)),
    "segment-file-overflow": (load[0]+8, struct.pack("<Q", 2**64-1)),
    "tls": (load[0], struct.pack("<I", 7)),
    "relocation-kind": (rela[4]+8, struct.pack("<Q", 7)),
    "relocation-code-target": (rela[4], struct.pack("<Q", load[1][3])),
    "relocation-outside-target": (rela[4], struct.pack("<Q", 2**64-8)),
    "relocation-outside-addend": (rela[4]+16, struct.pack("<q", -1)),
    "relocation-duplicate": (rela[4]+24, original[rela[4]:rela[4]+8]),
    "import": (symbols[4]+24+6, b"\0\0"),
    "ifunc": (symbols[4]+24+4, b"\x1a"),
}
with tempfile.TemporaryDirectory(prefix="boot-elf-negative-") as directory:
    path = Path(directory) / "case.so"
    subprocess.run([validator, "--validate", module], check=True)
    for name, (offset, mutation) in cases.items():
        data = bytearray(original)
        data[offset:offset+len(mutation)] = mutation
        path.write_bytes(data)
        result = subprocess.run([validator, "--validate", str(path)], timeout=3)
        assert result.returncode == 2, (name, result.returncode)
    for length in [0, 1, 7, 63, 64, len(original)//2, len(original)-1]:
        # The host runner's file-reader requires non-empty input.
        if not length:
            continue
        path.write_bytes(original[:length])
        result = subprocess.run([validator, "--validate", str(path)], timeout=3)
        assert result.returncode == 2, ("truncation", length, result.returncode)
print("BOOT-ELF-MALFORMED-REJECTION-PASS", len(cases)+6)
