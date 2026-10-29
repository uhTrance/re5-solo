"""Find the object vtable(s) of an MT class by name, and list their methods.

usage: python classvt.py <ClassName> [nslots]
Works via the getDTI() stub ("mov eax, offset DTI; ret") that each class's vtable contains.
"""
import re, struct, sys
from pathlib import Path

img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()
B = 0x400000
u = lambda a: struct.unpack_from('<I', img, a - B)[0]
name = sys.argv[1].encode()
nslots = int(sys.argv[2]) if len(sys.argv) > 2 else 16

for m in re.finditer(re.escape(name) + b'\0', img):
    if m.start() and img[m.start() - 1] != 0:
        continue  # suffix of a longer name
    s = B + m.start()
    for r in re.finditer(re.escape(struct.pack('<I', s)), img):
        dti = B + r.start() - 4
        if not 0xF7F000 <= u(dti) < 0x11E0000:
            continue
        stub = b'\xB8' + struct.pack('<I', dti) + b'\xC3'
        for f in re.finditer(re.escape(stub), img[:0xF7F000 - B]):
            fva = B + f.start()
            for v in re.finditer(re.escape(struct.pack('<I', fva)), img):
                slot = B + v.start()
                # vtable start: walk back while previous dword is a code pointer
                start = slot
                while 0x401000 <= u(start - 4) < 0xF7F000:
                    start -= 4
                print(f'{name.decode()} DTI {dti:#x}, getDTI {fva:#x} at vtable slot {(slot - start) // 4}, vtable {start:#x}')
                print('  ' + ' '.join(f'[{k}]{u(start + 4 * k):#x}' for k in range(nslots)))
