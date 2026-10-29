"""Unpack Sheva's normal-costume files (model, skeleton, cloth, motions...) as loose files for scene stand-ins.

With her player slot closed, her archive is never loaded, but in-engine scenes still create a
uBioActorPlayer for her that asks for pawn\\pl\\pl01\\model\\pl0100.mod. RE5 falls back to loose files
under nativePC_MT, so providing them lets those scenes play with her in them.

Playing as Sheva (re5_solo_sheva.txt) it is Chris whose slot is closed: --chris does the same for his
pawn\\pl\\pl00\\ files (uPl00ChrisNormal.arc, with its own manifest). --knife unpacks Chris's knife
(pawn\\wp\\wp01\\ from uWp01.arc): weapon packs that replace that archive ask for wp0100.mod without packing it,
and with Chris removed the game quits with "Failed open file ... wp0100.mod" (seen on 3-2's Josh segment).

usage: python loose_sheva.py [--chris|--knife]            install (writes loose_*_manifest.txt next to this script)
       python loose_sheva.py [--chris|--knife] --remove   delete every file the manifest lists
"""
import json, struct, sys, zlib
from pathlib import Path

GAME = Path(r'D:\Gaming\Launchers\Steam\steamapps\common\Resident Evil 5\nativePC_MT')
KIND = 'knife' if '--knife' in sys.argv else 'chris' if '--chris' in sys.argv else 'sheva'
ARC = GAME / 'Image' / 'Archive' / {'chris': 'uPl00ChrisNormal.arc', 'knife': 'uWp01.arc',
                                    'sheva': 'uPl01ShebaNormal.arc'}[KIND]
HERE = Path(__file__).parent
MANIFEST = HERE / f'loose_{KIND}_manifest.txt'
TYPES = json.loads((HERE.parent / 're' / 'arc_types.json').read_text())
# only that character's (or weapon's) own files
PREFIX = {'chris': 'pawn\\pl\\pl00\\', 'knife': 'pawn\\wp\\wp01\\', 'sheva': 'pawn\\pl\\pl01\\'}[KIND]


def entries(data):
    magic, ver, count = struct.unpack_from('<4sHH', data, 0)
    assert magic == b'ARC\0' and ver == 7, (magic, ver)
    for i in range(count):
        o = 8 + i * 0x50
        name = data[o:o + 64].split(b'\0')[0].decode()
        thash, csize, dsize, off = struct.unpack_from('<IIII', data, o + 64)
        yield name, f'{thash:08x}', data[off:off + csize], dsize & 0x1FFFFFFF


def sources():
    """(archive, name prefix) pairs to unpack."""
    yield ARC, PREFIX
    if KIND == 'knife':
        # Josh's segment on 3-2 also asked for Chris's knife motions (pawn\pl\pl00\motion\pl00wp01.lmt, from the
        # same archive): Josh plays Chris's pl00 motions, and nothing loads Chris's weapon archives without him.
        # So every weapon archive's own Chris motion file goes loose too (uWp0c.arc -> pl00wp0c.lmt).
        for arc in sorted(ARC.parent.glob('uWp??.arc')):
            yield arc, f'pawn\\pl\\pl00\\motion\\pl00wp{arc.stem[3:].lower()}'


def install():
    written = []
    for arc, prefix in sources():
        written += unpack(arc.read_bytes(), prefix)
    with MANIFEST.open('a') as f:
        f.writelines(p + '\n' for p in written)
    print(f'wrote {len(written)} files under {GAME}; listed in {MANIFEST}')


def unpack(data, prefix):
    written = []
    for name, thash, blob, size in entries(data):
        t = TYPES.get(thash)
        if not name.startswith(prefix):
            continue
        if not t or not t['ext']:
            print(f'skip (unknown type {thash}): {name}')
            continue
        raw = zlib.decompress(blob) if len(blob) != size else blob
        assert len(raw) == size, (name, len(raw), size)
        dest = GAME / f"{name}.{t['ext']}"
        if dest.exists():
            print(f'skip (already exists, not ours): {dest}')
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(raw)
        written.append(str(dest))
    return written


def remove():
    if not MANIFEST.exists():
        print('nothing installed')
        return
    paths = [Path(p) for p in MANIFEST.read_text().split('\n') if p]
    for p in paths:
        p.unlink(missing_ok=True)
    for d in sorted({p.parent for p in paths}, key=lambda d: -len(d.parts)):
        while d != GAME and d.exists() and not any(d.iterdir()):
            d.rmdir()
            d = d.parent
    MANIFEST.unlink()
    print(f'removed {len(paths)} files')


remove() if '--remove' in sys.argv else install()
