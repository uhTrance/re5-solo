"""Find objects in the running re5dx9.exe whose first dword is a given vtable.

usage: python findobj.py <hexvtable> [<hexvtable> ...]
"""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys


class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p),
                ('AllocationProtect', w.DWORD), ('PartitionId', w.WORD), ('RegionSize', ctypes.c_size_t),
                ('State', w.DWORD), ('Protect', w.DWORD), ('Type', w.DWORD)]


pid = int(subprocess.check_output(
    'powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32')
k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0410, False, pid)
targets = {int(v, 16): [] for v in sys.argv[1:]}
pats = {v: struct.pack('<I', v) for v in targets}

addr, mbi = 0x10000, MBI()
while addr < 0x7FFF0000 and k.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    size = mbi.RegionSize
    if mbi.State == 0x1000 and mbi.Type == 0x20000 and mbi.Protect in (0x04, 0x40):  # committed private RW
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
            raw = buf.raw[:n.value]
            for v, p in pats.items():
                i = raw.find(p)
                while i != -1:
                    if i % 4 == 0:
                        targets[v].append(addr + i)
                    i = raw.find(p, i + 1)
    addr += size

for v, hits in targets.items():
    print(f'{v:#x}: ' + ' '.join(f'{a:#x}' for a in hits[:20]) + (f' (+{len(hits) - 20} more)' if len(hits) > 20 else ''))
