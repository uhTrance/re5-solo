"""Live test: when the crank is nearly full, set its finished byte (+0x19F5) and stay-wound flag (+0x19A4 |= 4)."""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys, time
pid = int(subprocess.check_output('powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32'); k.OpenProcess.restype = w.HANDLE
h = k.OpenProcess(0x0438, False, pid)
def read(a, n):
    b = ctypes.create_string_buffer(n); r = ctypes.c_size_t()
    k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)); return b.raw
def write(a, d):
    r = ctypes.c_size_t(); k.WriteProcessMemory(h, ctypes.c_void_p(a), d, len(d), ctypes.byref(r))
o = int(sys.argv[1], 16); done = False; end = time.time() + float(sys.argv[2])
last = None
while time.time() < end:
    wind, mx = struct.unpack('<2f', read(o + 0x1970, 8))
    st = (round(wind), read(o + 0x19f5, 1)[0], hex(struct.unpack('<I', read(o + 0x19a4, 4))[0]), read(o + 0x19a0, 1)[0])
    if st != last: print(time.strftime('%H:%M:%S'), st, flush=True); last = st
    if not done and wind >= mx - 12:
        write(o + 0x19f5, b'\x01')
        f = struct.unpack('<I', read(o + 0x19a4, 4))[0] | 4
        write(o + 0x19a4, struct.pack('<I', f)); done = True; print('SET finished at wind', wind, flush=True)
    time.sleep(0.01)
