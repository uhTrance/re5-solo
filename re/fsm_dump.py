"""Print an RE5 .fsm (rAIFSM, XFS-serialised) as readable text: states, actions, transitions, conditions, params.

usage: python fsm_dump.py <file.fsm> [...]       (files from fsm_extract.py)

XFS layout (as found in s500): "XFS\\0", u16 ver, u16 ?, u32 class count, u32 def size, then per class an
offset (from +0x10) to {u32 hash, u32 prop count, props of 0x18: u32 name (from +0x10), u8 type, u8 attr,
u16 size, 16 pad}. Objects follow the defs: u16 (class << 1 | 1), u16 index, u32 size, then every property as
u32 count + values (type 1/2 = nested object, 0xE = C string, else `size` bytes). 0xFFFE = null object.
"""
import struct, sys


class Xfs:
    def __init__(self, d):
        self.d = d
        assert d[:4] == b'XFS\0'
        ncls, defsize = struct.unpack_from('<II', d, 8)
        self.classes = []
        for i in range(ncls):
            off = struct.unpack_from('<I', d, 0x10 + 4 * i)[0] + 0x10
            h, n = struct.unpack_from('<II', d, off)
            props = []
            for j in range(n):
                no, t, attr, size = struct.unpack_from('<IBBH', d, off + 8 + 0x18 * j)
                props.append((self.cstr(0x10 + no), t, attr, size))
            self.classes.append((h, props))
        self.pos = 0x10 + defsize

    def cstr(self, o):
        e = self.d.index(b'\0', o)
        return self.d[o:e].decode('latin1')

    def obj(self):
        if self.pos + 4 > len(self.d):  # some scenario files end their last array early
            return None
        tag, idx = struct.unpack_from('<HH', self.d, self.pos)
        if tag == 0xFFFE:
            self.pos += 4
            return None
        self.pos += 8
        h, props = self.classes[tag >> 1]
        out = {'_class': tag >> 1}
        for name, t, attr, size in props:
            if self.pos + 4 > len(self.d):
                break
            n = struct.unpack_from('<I', self.d, self.pos)[0]
            self.pos += 4
            vals = []
            for _ in range(n):
                if t in (1, 2):
                    vals.append(self.obj())
                elif t == 0x3A:  # resource: u8 kind, class name, path ("null" + empty path when unset)
                    self.pos += 1
                    s = self.cstr(self.pos)
                    self.pos += len(s.encode('latin1')) + 1
                    if s != 'null':
                        p = self.cstr(self.pos)
                        self.pos += len(p.encode('latin1')) + 1
                        s += ':' + p
                    else:
                        self.pos += 1
                    vals.append(s)
                elif t == 0xE:
                    s = self.cstr(self.pos)
                    self.pos += len(s.encode('latin1')) + 1
                    vals.append(s)
                else:
                    raw = self.d[self.pos:self.pos + size]
                    self.pos += size
                    if t == 0xC:
                        vals.append(round(struct.unpack('<f', raw)[0], 3))
                    elif size == 4:
                        vals.append(struct.unpack('<i' if t in (0xA,) else '<I', raw)[0])
                    elif size == 2:
                        vals.append(struct.unpack('<H', raw)[0])
                    elif size == 1:
                        vals.append(raw[0])
                    else:
                        vals.append(raw.hex())
            out[name] = vals if (attr & 0x20) or n != 1 else vals[0]
        return out


def params(plist):
    out = []
    for p in plist or []:
        if not p:
            continue
        d = {k[1:]: v for k, v in p.items()
             if not k.startswith('_') and k not in ('mParamName', 'mParameterName', 'mUIPos') and v not in ('', None)}
        out.append(' '.join(f'{k}={v}' for k, v in d.items()))
    return '; '.join(out)


def show(path):
    x = Xfs(open(path, 'rb').read())
    root = x.obj()
    print(f"=== {root.get('mFSMName')}  owner {root.get('mOwnerObjectName')}  start {root.get('mInitialStateName')}")
    fparams = {}
    for fp in root.get('mFuncParamList') or []:
        if fp:
            fparams[fp.get('mParamName')] = ' '.join(f'{k[1:]}={v}' for k, v in fp.items()
                                                    if not k.startswith('_') and k != 'mParamName')
    for st in root.get('mStateList') or []:
        if not st:
            continue
        print(f"  [{st.get('mStateName')}]")
        for a in st.get('mActionList') or []:
            if a:
                print(f"    do {a.get('mActionName')}  {fparams.get(a.get('mParamName'), '')}")
        for tr in st.get('mTransitionList') or []:
            if not tr:
                continue
            conds = []
            for c in tr.get('mConditionList') or []:
                if c:
                    conds.append(f"{c.get('mConditionName')}:{c.get('mCondition')} {params(c.get('mParameterList'))}")
            print(f"    -> {tr.get('mDestinationStateName')}  if {conds}")
        sub = st.get('mpSubMachine')
        if sub:
            print('    submachine', sub)


if __name__ == '__main__':
    for p in sys.argv[1:]:
        try:
            show(p)
        except Exception as e:
            print(f'=== {p}: parse error {e!r}')
