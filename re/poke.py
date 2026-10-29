"""Read-modify-write a dword in the running re5dx9.exe.

usage: python poke.py <expr> [or=<hex>] [and=<hex>] [set=<hex>]
expr is a hex address or pointer chain like 12340a4,10434 (each comma dereferences, then adds).
"""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys

pid = int(subprocess.check_output(
    'powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32')
k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0438, False, pid)  # QUERY_INFORMATION | VM_READ | VM_WRITE | VM_OPERATION


def rd(a):
    b = ctypes.create_string_buffer(4)
    if not k.ReadProcessMemory(h, ctypes.c_void_p(a), b, 4, None):
        raise SystemExit(f'read failed at {a:#x}')
    return struct.unpack('<I', b.raw)[0]


parts = [int(p, 16) for p in sys.argv[1].split(',')]
a = parts[0]
for off in parts[1:]:
    a = rd(a) + off
old = rd(a)
new = old
for op in sys.argv[2:]:
    kind, val = op.split('=')
    val = int(val, 16)
    new = new | val if kind == 'or' else new & val if kind == 'and' else val
if new != old:
    if not k.WriteProcessMemory(h, ctypes.c_void_p(a), struct.pack('<I', new), 4, None):
        raise SystemExit(f'write failed at {a:#x}')
print(f'{a:#x}: {old:#010x} -> {rd(a):#010x}')
