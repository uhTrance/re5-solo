"""Print every re5dx9.exe thread's EIP and the game-code return addresses on its stack.

usage: python stacks.py [maxaddrs]
Read-only (suspends nothing). Run while the game is stuck, e.g. on a fatal dialog.
"""
import ctypes, ctypes.wintypes as w, struct, subprocess, sys

MAX = int(sys.argv[1]) if len(sys.argv) > 1 else 24
LO, HI = 0x401000, 0x1928000
img = open(__file__.replace('stacks.py', 're5dx9_dump.bin'), 'rb').read()


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [('ContextFlags', w.DWORD), ('Dr', w.DWORD * 6), ('FloatSave', ctypes.c_byte * 112),
                ('SegGs', w.DWORD), ('SegFs', w.DWORD), ('SegEs', w.DWORD), ('SegDs', w.DWORD),
                ('Edi', w.DWORD), ('Esi', w.DWORD), ('Ebx', w.DWORD), ('Edx', w.DWORD), ('Ecx', w.DWORD),
                ('Eax', w.DWORD), ('Ebp', w.DWORD), ('Eip', w.DWORD), ('SegCs', w.DWORD), ('EFlags', w.DWORD),
                ('Esp', w.DWORD), ('SegSs', w.DWORD), ('ExtendedRegisters', ctypes.c_byte * 512)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD),
                ('th32OwnerProcessID', w.DWORD), ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]


def is_ret(a):
    if not LO + 6 <= a < HI:
        return False
    o = a - 0x400000
    p = img[o - 6:o]
    return p[1] == 0xE8 or (p[4] == 0xFF and (p[5] & 0x38) == 0x10) or \
        (p[3] == 0xFF and (p[4] & 0x38) == 0x10) or (p[0] == 0xFF and (p[1] & 0x38) == 0x10)


pid = int(subprocess.check_output(
    'powershell -NoProfile -Command "(Get-Process re5dx9).Id"', text=True).split()[0])
k = ctypes.WinDLL('kernel32')
k.OpenProcess.restype = w.HANDLE
k.OpenThread.restype = w.HANDLE
k.CreateToolhelp32Snapshot.restype = w.HANDLE
h = k.OpenProcess(0x0410, False, pid)
snap = k.CreateToolhelp32Snapshot(4, 0)
te = THREADENTRY32()
te.dwSize = ctypes.sizeof(te)
ok = k.Thread32First(snap, ctypes.byref(te))
while ok:
    if te.th32OwnerProcessID == pid:
        th = k.OpenThread(0x0048, False, te.th32ThreadID)  # GET_CONTEXT | QUERY_INFORMATION
        ctx = WOW64_CONTEXT()
        ctx.ContextFlags = 0x10003  # WOW64_CONTEXT_CONTROL | INTEGER
        if th and k.Wow64GetThreadContext(th, ctypes.byref(ctx)):
            buf = ctypes.create_string_buffer(0x8000)
            n = ctypes.c_size_t()
            k.ReadProcessMemory(h, ctypes.c_void_p(ctx.Esp), buf, 0x8000, ctypes.byref(n))
            rets = [v for (v,) in struct.iter_unpack('<I', buf.raw[:n.value - n.value % 4]) if is_ret(v)]
            if rets:
                print(f'tid {te.th32ThreadID} eip {ctx.Eip:#x}: ' + ' '.join(f'{a:#x}' for a in rets[:MAX]))
        if th:
            k.CloseHandle(th)
    ok = k.Thread32Next(snap, ctypes.byref(te))
