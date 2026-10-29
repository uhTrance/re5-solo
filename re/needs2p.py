"""Find code that needs a second player: calls to getPlayer(1) (0x7E6220 with push 1) or the partner getter
(0xB5BED0), grouped by the class whose vtable holds the enclosing function (or that function's callers).

usage: python needs2p.py > needs2p.txt
"""
import re, struct
from collections import defaultdict
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()
B = 0x400000
TEXT_LO, TEXT_HI = 0x401000, 0xd00000
u = lambda a: struct.unpack_from('<I', img, a - B)[0]
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.skipdata = True

GET_PLAYER, PARTNER = 0x7e6220, 0xb5bed0


def func_start(a):
    """Walk back to int3 padding (MSVC aligns functions)."""
    while a > TEXT_LO and not (img[a - 1 - B] == 0xCC and img[a - 2 - B] == 0xCC):
        a -= 1
    return a


def class_of_vtable_slot(slot):
    """Name of the class whose vtable contains `slot` (walk back to the vtable start, read getDTI)."""
    s = slot
    for _ in range(0x400):
        if not (TEXT_LO <= u(s - 4) < TEXT_HI):
            break
        s -= 4
    for start in (s,):
        try:
            f = u(start + 0x10)
            if img[f - B] == 0xB8 and img[f + 5 - B] == 0xC3:
                d = u(f + 1)
                n = u(d + 4)
                t = img[n - B:n - B + 60]
                return t[:t.index(b'\0')].decode('latin1')
        except Exception:
            pass
    return None


# index: code pointer -> data slots referencing it (vtables), and direct call sites
code_refs = defaultdict(list)

data = img[0xf00000 - B:0x1300000 - B]
for off in range(0, len(data) - 3, 4):
    v = struct.unpack_from('<I', data, off)[0]
    if TEXT_LO <= v < TEXT_HI:
        code_refs[v].append(0xf00000 + off)

calls = defaultdict(list)
text = img[TEXT_LO - B:TEXT_HI - B]
for off in range(len(text) - 5):
    if text[off] == 0xE8:
        t = (TEXT_LO + off + 5 + struct.unpack_from('<i', text, off + 1)[0]) & 0xffffffff
        if TEXT_LO <= t < TEXT_HI:
            calls[t].append(TEXT_LO + off)


def owners(fn, depth=0, seen=None):
    seen = seen or set()
    if fn in seen or depth > 3:
        return set()
    seen.add(fn)
    names = {c for c in (class_of_vtable_slot(r) for r in code_refs.get(fn, [])) if c}
    if names:
        return names
    out = set()
    for c in calls.get(fn, [])[:12]:
        out |= owners(func_start(c), depth + 1, seen)
    return out


hits = defaultdict(list)  # function -> [(site, kind)]
for site in calls.get(GET_PLAYER, []):
    pre = img[site - 8 - B:site - B]
    if b'\x6a\x01' in pre[-6:]:
        hits[func_start(site)].append((site, 'getPlayer(1)'))
for site in calls.get(PARTNER, []):
    hits[func_start(site)].append((site, 'partner'))

by_class = defaultdict(list)
for fn, sites in hits.items():
    for name in sorted(owners(fn)) or ['?']:
        by_class[name].append((fn, sites))

for name in sorted(by_class, key=lambda n: (n == '?', n)):
    print(name)
    for fn, sites in sorted(by_class[name]):
        print(f'  {fn:#x}: ' + ', '.join(f'{k} @{s:#x}' for s, k in sites))
