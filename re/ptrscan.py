"""List the objects an object points to, by class name.

usage: python ptrscan.py <hexaddr> <hexsize> [filter]
Each pointer-looking dword is traced back to its containing MT object (vtable + class via getDTI).
"""
import re, struct, sys
from pathlib import Path
from snap import read

img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()
B = 0x400000
CODE_END = 0xF6D000
u = lambda a: struct.unpack_from('<I', img, a - B)[0]
stubs = {B + m.start(): struct.unpack('<I', m.group(1))[0]
         for m in re.finditer(rb'\xB8(....)\xC3', img[:CODE_END - B], re.S)}
_cls = {}


def cname(vt):
    if vt in _cls:
        return _cls[vt]
    name = None
    for k in range(48):
        f = u(vt + 4 * k)
        if not 0x401000 <= f < CODE_END:
            break
        d = stubs.get(f)
        if d and 0x1100000 <= d < 0x1300000:
            n = u(d + 4)
            if B <= n < B + len(img):
                s = img[n - B:n - B + 64].split(b'\0')[0]
                if s and all(32 < c < 127 for c in s):
                    name = s.decode()
                    break
    _cls[vt] = name
    return name


def is_vt(v):
    return 0xEB3000 <= v < 0x11E0000 and v % 4 == 0 and all(0x401000 <= u(v + 4 * k) < CODE_END for k in range(3))


def owner(a, back=0x3000):
    start = (a - back) & ~3
    try:
        mem = read(start, a - start + 4)
    except SystemExit:
        return None
    for off in range(len(mem) - 4, -1, -4):
        v = struct.unpack_from('<I', mem, off)[0]
        if is_vt(v):
            return start + off, v, cname(v)
    return None


base, size = int(sys.argv[1], 16), int(sys.argv[2], 16)
flt = sys.argv[3].lower() if len(sys.argv) > 3 else ''
mem = read(base, size)
seen = set()
for off in range(0, size, 4):
    v = struct.unpack_from('<I', mem, off)[0]
    if not 0x01000000 < v < 0x7FFF0000 or 0x400000 <= v < 0x1930000:
        continue
    o = owner(v)
    if not o or not o[2]:
        continue
    if flt and flt not in o[2].lower():
        continue
    key = (off, o[0])
    if o[0] in seen and not flt:
        continue
    seen.add(o[0])
    print(f'+{off:04x} -> {v:#x} = {o[2]} @ {o[0]:#x} +{v - o[0]:#x}')
