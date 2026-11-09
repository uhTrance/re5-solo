"""Read memory from a minidump: python dmpread.py <file.dmp> <hex addr> [dwords=16]
Prints the dwords if the dump holds that address (MemoryListStream / Memory64ListStream)."""
import struct
import sys


def regions(data):
    sig, ver, nstreams, rva = struct.unpack_from('<4sIII', data, 0)
    for i in range(nstreams):
        stype, size, srva = struct.unpack_from('<III', data, rva + 12 * i)
        if stype == 5:  # MemoryListStream
            (n,) = struct.unpack_from('<I', data, srva)
            for j in range(n):
                start, dsize, drva = struct.unpack_from('<QII', data, srva + 4 + 16 * j)
                yield start, dsize, drva
        elif stype == 9:  # Memory64ListStream
            n, base = struct.unpack_from('<QQ', data, srva)
            for j in range(n):
                start, dsize = struct.unpack_from('<QQ', data, srva + 16 + 16 * j)
                yield start, dsize, base
                base += dsize


def read(data, addr, size):
    for start, dsize, drva in regions(data):
        if start <= addr and addr + size <= start + dsize:
            off = drva + addr - start
            return data[off:off + size]
    return None


if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    addr = int(sys.argv[2], 16)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    for k in range(n):
        b = read(data, addr + 4 * k, 4)
        print(f'{addr + 4 * k:08X}: ' + (f'{struct.unpack("<I", b)[0]:08X}' if b else '--'))
