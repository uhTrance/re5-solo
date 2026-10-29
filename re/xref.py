"""Find code references to strings in the RE5 memory dump.

usage: python xref.py <string> [<string> ...]
Prints each string's address and every instruction that uses that address as an immediate/displacement.
"""
import re, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

BASE = 0x400000
TEXT_END = 0xb4a000  # first section (code)
img = (Path(__file__).parent / 're5dx9_dump.bin').read_bytes()

def addrs_of(s):
    return [BASE + m.start() for m in re.finditer(re.escape(s.encode()) + b'\0', img)]

def xrefs(va):
    pat = struct.pack('<I', va)
    for m in re.finditer(re.escape(pat), img[:TEXT_END - BASE]):
        yield BASE + m.start()

md = Cs(CS_ARCH_X86, CS_MODE_32)

def insn_at(ref):
    # the immediate sits 1-6 bytes into an instruction; pick a start that decodes across it
    for back in range(1, 8):
        start = ref - back
        off = start - BASE
        for i in md.disasm(img[off:off + 16], start):
            if i.address <= ref < i.address + i.size:
                return i
            break
    return None

for s in sys.argv[1:]:
    for va in addrs_of(s):
        print(f'{s!r} @ {va:#x}')
        for ref in xrefs(va):
            i = insn_at(ref)
            print(f'    {ref:#x}  {i.address:#x}: {i.mnemonic} {i.op_str}' if i else f'    {ref:#x}  (data)')
