"""RE5 (Steam) savedata.bin: decode, encode and checksum.

Format (from the file itself): every 8-byte block is XORed with the previous *encoded* block, the first with a
fixed seed. Decoded, +0x0 is the Steam ID, +0x8 a checksum: the 32-bit sum of the dwords at 0x10..0x3BAC and
0x3BB0..0x52AC.

usage: python re5save.py decode <in.bin> <out.raw>
       python re5save.py encode <in.raw> <out.bin>   (fixes the checksum first)
"""
import struct, sys

SEED = bytes([0x00, 0x21, 0x11, 0x08, 0xC0, 0x4B, 0x00, 0x00])


def decode(enc: bytes) -> bytearray:
    out, key = bytearray(), SEED
    for i in range(0, len(enc), 8):
        block = enc[i:i + 8]
        out += bytes(a ^ b for a, b in zip(block, key))
        key = block
    return out


def encode(raw: bytes) -> bytes:
    out, key = bytearray(), SEED
    for i in range(0, len(raw), 8):
        block = bytes(a ^ b for a, b in zip(raw[i:i + 8], key))
        out += block
        key = block
    return bytes(out)


def checksum(raw: bytes) -> int:
    total = sum(struct.unpack_from('<3815I', raw, 0x10))
    total += sum(struct.unpack_from('<1472I', raw, 0x3BB0))
    return total & 0xFFFFFFFF


def fix_checksum(raw: bytearray) -> bytearray:
    struct.pack_into('<I', raw, 8, checksum(raw))
    return raw


if __name__ == '__main__':
    cmd, src, dst = sys.argv[1:4]
    data = open(src, 'rb').read()
    if cmd == 'decode':
        raw = decode(data)
        ok = struct.unpack_from('<I', raw, 8)[0] == checksum(raw)
        print('checksum', 'OK' if ok else 'MISMATCH')
        open(dst, 'wb').write(raw)
    else:
        open(dst, 'wb').write(encode(fix_checksum(bytearray(data))))
