"""Build the RE5 archive type-hash -> (class, extension) table from the dump; writes arc_types.json.

Type hash = jamcrc(class name) & 0x7fffffff. The extension comes from the class vtable's
"mov eax, offset 'ext'; ret" stub next to its getDTI() stub ("mov eax, offset DTI; ret").
"""
import json, re, struct, zlib
from pathlib import Path

HERE = Path(__file__).parent
img = (HERE / 're5dx9_dump.bin').read_bytes()
B = 0x400000
CODE_END, RDATA = 0xF6D000, (0xEB3000, 0x11E0000)  # section 0xEB3000 mixes code and vtables
u = lambda a: struct.unpack_from('<I', img, a - B)[0]

# 1. every "mov eax, imm32; ret" stub in code: fn -> imm
stubs = {}
for m in re.finditer(rb'\xB8(....)\xC3', img[:CODE_END - B], re.S):
    stubs[B + m.start()] = struct.unpack('<I', m.group(1))[0]

# 2. every rdata dword that points at a stub: stub fn -> [slot addresses]
slots = {}
for off in range(RDATA[0] - B, RDATA[1] - B, 4):
    v = struct.unpack_from('<I', img, off)[0]
    if v in stubs:
        slots.setdefault(v, []).append(B + off)


def cstr(a, n=64):
    if not B <= a < B + len(img):
        return None
    s = img[a - B:a - B + n].split(b'\0')[0]
    return s.decode() if s and all(32 < c < 127 for c in s) else None


# 3. DTIs: getDTI stub returns a DTI whose +4 is the class name
table = {}
for fn, dti in stubs.items():
    if not 0x1100000 <= dti < 0x1300000:
        continue
    try:
        name = cstr(u(dti + 4))
    except struct.error:
        continue
    if not name or not re.fullmatch(r'r[A-Z]\w+', name):
        continue
    ext = None
    for slot in slots.get(fn, []):
        # scan outward within this vtable only (stop at the first non-code dword either way)
        for step in (1, -1):
            k = 0
            while not ext and abs(k) < 64:
                k += step
                f = u(slot + 4 * k)
                if not 0x401000 <= f < CODE_END:
                    break
                s = cstr(stubs[f], 8) if f in stubs else None
                if s and 2 <= len(s) <= 4 and s.isalnum() and s.islower():
                    ext = s
        if ext:
            break
    h = (zlib.crc32(name.encode()) ^ 0xFFFFFFFF) & 0x7FFFFFFF
    table[f'{h:08x}'] = {'class': name, 'ext': ext}

(HERE / 'arc_types.json').write_text(json.dumps(dict(sorted(table.items())), indent=1))
print(f'{len(table)} resource classes, {sum(1 for v in table.values() if v["ext"])} with extensions')
