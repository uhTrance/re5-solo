"""Read memory from the running re5dx9.exe. usage: python peek.py <expr> [bytes]
expr: hex address, or a pointer chain like 12340a4,1042c,478 (each comma dereferences then adds)."""
import ctypes, ctypes.wintypes as w, subprocess, struct, sys
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0410, False, pid)
def read(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)): raise SystemExit(f'read failed at {a:#x}')
    return b.raw
parts = [int(p, 16) for p in sys.argv[1].split(',')]
a = parts[0]
for off in parts[1:]:
    a = struct.unpack('<I', read(a, 4))[0] + off
n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 64
data = read(a, n)
print(f'{a:#x}:')
for i in range(0, n, 16):
    row = data[i:i+16]
    print(f'  +{i:03x}  ' + ' '.join(f'{x:08x}' for x in struct.unpack(f'<{len(row)//4}I', row)))
