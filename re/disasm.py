"""Disassemble the RE5 dump.

usage: python dis.py <hexaddr> [count]
"""
import sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()
B = 0x400000
a = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
md = Cs(CS_ARCH_X86, CS_MODE_32)
for k, i in enumerate(md.disasm(img[a - B:a - B + n * 8], a)):
    if k >= n:
        break
    print(f'{i.address:#x}: {i.mnemonic} {i.op_str}')
