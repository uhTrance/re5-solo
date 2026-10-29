"""Live: print which player slots each zone counts as inside (area manager [[0x11E8660]+0xDC]), with Chris's position."""
import struct, sys, time
from snap import read
u = lambda a: struct.unpack('<I', read(a, 4))[0]
zones = [int(z) for z in sys.argv[2:]] or [3]
end = time.time() + float(sys.argv[1]); last = None
while time.time() < end:
    mgr = u(u(0x11E8660) + 0xDC); n = u(mgr + 8); arr = u(mgr + 0x14)
    out = []
    for z in zones:
        a = u(arr + 4 * z) if z < n else 0
        d = u(a + 8) if a else 0
        out.append((z, hex(u(d + 0x10)) if d else '-'))
    chris = u(u(0x11B27DC) + 0x24)
    pos = tuple(round(v) for v in struct.unpack('<3f', read(chris + 0x30, 12)))
    st = (tuple(out), n)
    if st != last: print(time.strftime('%H:%M:%S'), 'zones', n, out, 'chris', pos, flush=True); last = st
    time.sleep(0.1)
