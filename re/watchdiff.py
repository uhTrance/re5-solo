"""Log field changes over time in some memory blocks (to find what a scripted moment changes).

usage: python watchdiff.py <seconds> <label>=<hexaddr>:<hexsize> ...   (addr may be a chain like 12340a4,0)
First 3 s learn which dwords change by themselves (ignored), then log every other change with a timestamp.
"""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys, time
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0410, False, pid)
def read(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)); return b.raw[:r.value].ljust(n, b'\0')
def resolve(expr):
    parts = [int(p, 16) for p in expr.split(',')]; a = parts[0]
    for off in parts[1:]: a = struct.unpack('<I', read(a, 4))[0] + off
    return a
blocks = []
for arg in sys.argv[2:]:
    label, rest = arg.split('='); addr, size = rest.split(':')
    blocks.append((label, resolve(addr), int(size, 16)))
noisy = [set() for _ in blocks]
prev = [read(a, n) for _, a, n in blocks]
t0 = time.time(); end = t0 + float(sys.argv[1])
print('watching', [(l, hex(a), hex(n)) for l, a, n in blocks], flush=True)
while time.time() < end:
    time.sleep(0.05)
    learning = time.time() - t0 < 3
    for i, (label, a, n) in enumerate(blocks):
        cur = read(a, n)
        for off in range(0, n, 4):
            if cur[off:off+4] != prev[i][off:off+4]:
                if learning: noisy[i].add(off)
                elif off not in noisy[i]:
                    o, c = struct.unpack_from('<I', prev[i], off)[0], struct.unpack_from('<I', cur, off)[0]
                    f = struct.unpack_from('<f', cur, off)[0]
                    print(f'{time.strftime("%H:%M:%S")} {label}+{off:#x}: {o:08x} -> {c:08x}' + (f' ({f:.2f})' if 1e-3 < abs(f) < 1e6 else ''), flush=True)
                    noisy[i].add(off) if False else None
        prev[i] = cur
