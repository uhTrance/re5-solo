"""Identify the MT Framework object containing an address in the running game.

usage: python whatis.py <hexaddr> [maxback]
Walks back from the address looking for a dword that is a vtable whose slot points into code, then
finds the DTI with that vtable's class name by scanning the dump for the getDTI pattern.
"""
import struct, sys
from pathlib import Path
from snap import read  # noqa: E402  (reads live process memory)

img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()
B = 0x400000
u = lambda a: struct.unpack_from('<I', img, a - B)[0]
CODE = (0x401000, 0xF7F000)
RDATA = (0xF7F000, 0x11E0000)


def is_vtable(v):
    if not RDATA[0] <= v < RDATA[1] or v % 4:
        return False
    return all(CODE[0] <= u(v + 4 * k) < CODE[1] for k in range(4))


def dti_name(dti):
    try:
        name = u(dti + 4)
        s = img[name - B:name - B + 64].split(b'\0')[0]
        return s.decode('ascii') if s and all(32 <= c < 127 for c in s) else None
    except Exception:
        return None


def class_of(vt):
    # MT getDTI() is usually "mov eax, offset DTI; ret" in one of the first vtable slots
    for k in range(12):
        f = u(vt + 4 * k)
        code = img[f - B:f - B + 6]
        if code[0] == 0xB8 and code[5] == 0xC3:
            n = dti_name(struct.unpack_from('<I', code, 1)[0])
            if n:
                return n
    return '?'


addr = int(sys.argv[1], 16)
maxback = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x4000
start = (addr - maxback) & ~3
mem = read(start, addr - start + 4)
for off in range(len(mem) - 4, -1, -4):
    v = struct.unpack_from('<I', mem, off)[0]
    if is_vtable(v):
        base = start + off
        print(f'{addr:#x} is +{addr - base:#x} into object {base:#x}, vtable {v:#x}, class {class_of(v)}')
        break
else:
    print('no vtable found')
