"""Emulate <Class>::customSetup (vtable slot 139) for every object id and record the
fields it writes on the GameObject.  Read-only on the exe.
Usage: python run_customsetup.py <exe> <vtables.json> <classes.json> <out.json> [ids]
"""
import sys, json, collections
sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from emu import Image, Emu, i2f

exe, vtj, clsj, out = sys.argv[1:5]
only = set(map(int, sys.argv[5].split(','))) if len(sys.argv) > 5 else None
img = Image(exe)
vt = json.load(open(vtj))
slot139 = {name: int(lst[0][1], 16) for name, lst in vt.items()}
classes = json.load(open(clsj))

CHAIN = {0x28e020, 0x2a1f80, 0x64a320, 0x641be0, 0x639e40, 0x660ac0, 0x63b8e0}
HELPERS = {0x293ef0, 0x2c16b0}
FLOATF = {0x2fc, 0x300, 0x394, 0x398, 0x38c, 0x428, 0x42c, 0x43c, 0x56c}

# init-time defaults established by GameObject ctor (0x208980) + GameObject::init (0x28ae70)
# (and class-specific init functions seen writing the type field)
INIT_TYPE = {'CheckpointGameObject': 0x14}


class LinEmu(Emu):
    """adds a tiny symbolic float: ('lin', field_off, k) == k * obj.field"""
    def exec(self, st, ins, mn, ops, nxt):
        from capstone import x86_const as X
        if mn == 'movss' and ops[1].type == X.X86_OP_MEM:
            a = self.addr(st, ins, ops[1])
            if isinstance(a, tuple) and a[0] == 'obj' and a not in st.mem:
                self.opset(st, ins, ops[0], ('lin', a[1], 1.0))
                return None
        if mn == 'mulss':
            dst, src = ops
            a = self.opget(st, ins, dst)
            if isinstance(a, tuple) and a[0] == 'lin':
                if src.type == X.X86_OP_MEM:
                    ad = self.addr(st, ins, src)
                    b = self.img.readf(ad) if isinstance(ad, int) else None
                else:
                    b = self.opget(st, ins, src)
                self.opset(st, ins, dst, ('lin', a[1], a[2] * b) if isinstance(b, float) else None)
                return None
        if mn == 'addss':
            dst, src = ops
            a = self.opget(st, ins, dst)
            if src.type == X.X86_OP_REG and src.reg == dst.reg and isinstance(a, tuple) and a[0] == 'lin':
                self.opset(st, ins, dst, ('lin', a[1], a[2] * 2))
                return None
        return super().exec(st, ins, mn, ops, nxt)


emu = LinEmu(img, follow=CHAIN | HELPERS, max_states=1500, max_steps=60000)


def fmt(off, val, size, is_float):
    if isinstance(val, tuple):
        if val[0] == 'lin':
            return f'{round(val[2], 4)}*field{val[1]:#x}'
        return str(val)
    if val is None:
        return None
    if off in FLOATF or is_float:
        if isinstance(val, float):
            return round(val, 4)
        return round(i2f(val), 4)
    if isinstance(val, int) and size == 4 and val >= 0x80000000:
        return val - (1 << 32)
    return val


results = {}
for sid, info in classes.items():
    oid = int(sid)
    if only and oid not in only:
        continue
    cls = [c for c in info['classes'] if c][0]
    entry = slot139[cls]
    regs = {'rcx': ('obj', 0), 'rsp': None, 'rbp': None}
    init_mem = {('obj', 0x3a0): (INIT_TYPE.get(cls, 0), 4), ('obj', 0x3a8): (7, 4),
                ('obj', 0x394): (1.0, 4), ('obj', 0x398): (1.0, 4)}
    done, aborted = emu.run(entry, regs, oid, init_mem=init_mem)
    per_field = collections.defaultdict(collections.Counter)
    calls = collections.Counter()
    for st in done:
        last = {}
        for (a, val, size, isf, addr) in st.writes:
            key = f'{a[1]:#x}' if a[0] == 'obj' else f'{a[1]:#x}->{a[2]:#x}'
            last[key] = fmt(a[1] if a[0] == 'obj' else -1, val, size, isf)
        for k, v in last.items():
            per_field[k][json.dumps(v)] += 1
        for k in set(per_field) - set(last):
            per_field[k]['"<unset>"'] += 1
        for c in st.calls:
            if c[2] == ('obj', 0) and isinstance(c[1], int):
                calls[hex(c[1])] += 1
    # fields never written in some paths that were seen later
    npaths = len(done)
    fields = {}
    for k, cnt in per_field.items():
        tot = sum(cnt.values())
        if tot < npaths:
            cnt['"<unset>"'] += npaths - tot
        fields[k] = {kk: vv for kk, vv in cnt.items()}
    results[oid] = {'class': cls, 'paths': npaths, 'aborted': aborted, 'fields': fields,
                    'this_calls': dict(calls), 'abort_reasons': dict(emu.reasons.most_common(5))}

json.dump(results, open(out, 'w'), indent=0)
ab = [k for k, v in results.items() if v['aborted'] or not v['paths']]
print('ids', len(results), 'with aborted paths', len(ab), ab[:30])
