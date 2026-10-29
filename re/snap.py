"""Snapshot / diff memory of the running re5dx9.exe.

usage:
  python snap.py save <name> <hexaddr> <hexsize>   -> snaps/<name>.bin (+ address in snaps/<name>.txt)
  python snap.py diff <a> <b>                      -> dwords that differ, as offsets from the base
"""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys
from pathlib import Path

SNAPS = Path(__file__).parent / 'snaps'


def read(addr, size):
    pid = int(subprocess.check_output(
        'powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
    k = ctypes.WinDLL('kernel32')
    k.OpenProcess.restype = w.HANDLE
    h = k.OpenProcess(0x0410, False, pid)
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        raise SystemExit(f'read failed at {addr:#x}')
    return buf.raw


if __name__ == '__main__' and sys.argv[1] == 'save':
    name, addr, size = sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
    SNAPS.mkdir(exist_ok=True)
    data = read(addr, size)
    (SNAPS / f'{name}.bin').write_bytes(data)
    (SNAPS / f'{name}.txt').write_text(f'{addr:#x}\n')
    print(f'saved {name}: {size:#x} bytes at {addr:#x}, first dword {struct.unpack_from("<I", data)[0]:#x}')
elif __name__ == '__main__':
    a = (SNAPS / f'{sys.argv[2]}.bin').read_bytes()
    b = (SNAPS / f'{sys.argv[3]}.bin').read_bytes()
    for off in range(0, min(len(a), len(b)) - 3, 4):
        x, y = struct.unpack_from('<I', a, off)[0], struct.unpack_from('<I', b, off)[0]
        if x != y:
            fx, fy = struct.unpack_from('<f', a, off)[0], struct.unpack_from('<f', b, off)[0]
            print(f'+{off:04x}: {x:08x} -> {y:08x}   (float {fx:.3g} -> {fy:.3g})')
