"""Dump a level script's current cAIFSM state: transitions with their conditions (property op value) and actions.
usage: python fsmstate.py <cFSMOrder hex>   (script address as the tracer logs it)"""
import ctypes, ctypes.wintypes as w, subprocess, struct, sys
from pathlib import Path
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x410, False, pid)
def rd(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    return b.raw if a and k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)) else b'\0' * n
def u(a): return struct.unpack('<I', rd(a, 4))[0]
img = (Path(r'D:\Apps\Coding Stuff\re5-solo\re') / 're5dx9_dump.bin').read_bytes()
B = 0x400000
def cls(obj):
    """class name via vtable getDTI stub (vtable[4]: mov eax, DTI; ret) or the name at DTI+4"""
    vt = u(obj)
    try:
        f = struct.unpack_from('<I', img, vt + 0x10 - B)[0]
        if img[f - B] == 0xB8:
            dti = struct.unpack_from('<I', img, f + 1 - B)[0]
            name = struct.unpack_from('<I', img, dti + 4 - B)[0]
            s = img[name - B:name - B + 60]
            return s[:s.index(b'\0')].decode()
    except Exception: pass
    return f'vt {vt:08X}'
def param(p):
    c = cls(p)
    if 'Property' in c:
        name = rd(p + 0x68, 32).split(b'\0')[0].decode('ascii', 'replace')
        return f'{name}(id {u(p + 0x10):X})'
    if 'Const' in c:
        return f'{u(p + 0x10):X}'
    return f'<{c} {p:08X}>'

order = int(sys.argv[1], 16)
core = u(order + 0x10)
state = u(core + 8)
print(f'order {order:08X} core {core:08X} current state {state:08X} ({cls(state)})')
def dump_state(st, depth=0):
    n = u(st + 0x10); arr = u(st + 0x18)
    print('  ' * depth + f'state {st:08X}: {n} transitions')
    for i in range(min(n, 16)):
        t = u(arr + 4 * i)
        target = u(t + 0xC); cond = u(t + 0x20) - 0x50 if u(t + 0x20) else 0
        desc = 'always'
        if cond:
            left, right, op = u(cond + 8), u(cond + 0xC), u(cond + 0x10)
            desc = f'{param(left)} op{op} {param(right)}'
        print('  ' * depth + f'  -> {target:08X} if {desc}')
dump_state(state)
