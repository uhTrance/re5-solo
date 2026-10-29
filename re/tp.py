"""Teleport Chris (test): python tp.py <x> <y> <z> -- writes every copy of his position at once."""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys, time
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0438, False, pid)
def read(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)); return b.raw
def write(a, data):
    r = ctypes.c_size_t(); k.WriteProcessMemory(h, ctypes.c_void_p(a), data, len(data), ctypes.byref(r))
ch = struct.unpack('<I', read(struct.unpack('<I', read(0x11b27dc, 4))[0] + 0x24, 4))[0]
pos = struct.pack('<3f', *map(float, sys.argv[1:4]))
OFFS = [0x30, 0x90, 0xd0, 0x110, 0x1050, 0x1060, 0x10b0, 0x1170, 0x11d0, 0x1370, 0x1380, 0x1390, 0x13a0, 0x1b10]
for o in OFFS: write(ch + o, pos)
time.sleep(1.5)
print([round(v, 1) for v in struct.unpack('<3f', read(ch + 0x30, 12))])
