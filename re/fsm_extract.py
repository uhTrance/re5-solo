"""Extract every .fsm (rAIFSM) from a stage archive into re/fsm/<arc>/.

usage: python fsm_extract.py s500
"""
import json, struct, sys, zlib
from pathlib import Path

GAME = Path(r'D:\Gaming\Launchers\Steam\steamapps\common\Resident Evil 5\nativePC_MT\Image\Archive')
HERE = Path(__file__).parent
TYPES = json.loads((HERE / 'arc_types.json').read_text())

name = sys.argv[1]
data = (GAME / f'{name}.arc').read_bytes()
magic, ver, count = struct.unpack_from('<4sHH', data, 0)
assert magic == b'ARC\0' and ver == 7, (magic, ver)
out = HERE / 'fsm' / name
out.mkdir(parents=True, exist_ok=True)
for i in range(count):
    o = 8 + i * 0x50
    path = data[o:o + 64].split(b'\0')[0].decode()
    thash, csize, dsize, off = struct.unpack_from('<IIII', data, o + 64)
    t = TYPES.get(f'{thash:08x}')
    ext = t['ext'] if t else None
    if ext != 'fsm':
        continue
    raw = zlib.decompress(data[off:off + csize])
    dest = out / (path.replace(chr(92), '__') + '.fsm')
    dest.write_bytes(raw)
    print(f'{len(raw):7} {path}')
