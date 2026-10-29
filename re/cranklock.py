"""Live test: once a crank's wind reaches cap (fraction of max), pin wind (+0x1970, +0x17FC) and percent (+0x197C) there."""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys, time
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0438, False, pid)
def read(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)); return b.raw
def write(a, d):
    r = ctypes.c_size_t(); k.WriteProcessMemory(h, ctypes.c_void_p(a), d, len(d), ctypes.byref(r))
o = int(sys.argv[1], 16); frac = float(sys.argv[2]); end = time.time() + float(sys.argv[3])
locked = False
while time.time() < end:
    wind, mx = struct.unpack('<2f', read(o + 0x1970, 8))
    cap = mx * frac
    if not locked and wind >= cap: locked = True; print('locked at', wind, flush=True)
    if locked:
        write(o + 0x1970, struct.pack('<f', cap)); write(o + 0x17fc, struct.pack('<f', cap))
        write(o + 0x197c, struct.pack('<f', frac * 100))
    time.sleep(0.005)
