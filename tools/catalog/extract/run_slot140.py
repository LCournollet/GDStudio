"""Emulate GameObject vtable slot 140 (0x2acb40, child-sprite setup) per id and record
writes to type / size / hitbox fields only. Read-only on exe."""
import sys, json, collections, time
sys.path.insert(0, __import__('os').path.dirname(__file__))
from emu import Image, Emu, i2f
exe, clsj, out = sys.argv[1:4]
only = set(map(int, sys.argv[4].split(','))) if len(sys.argv) > 4 else None
img = Image(exe)
classes = json.load(open(clsj))
WATCH = {0x3a0, 0x2fc, 0x300, 0x394, 0x398, 0x38c}
SITES = {0x2b2e0f, 0x2b7922, 0x2b8453, 0x2b851f, 0x2b820a, 0x2bab3a}
class E(Emu):
    def exec(self, st, ins, mn, ops, nxt):
        if ins.address in SITES:
            st.note.append(('site', ins.address))
        if mn == 'lea' and ops[1].type == 3 and ins.reg_name(ops[1].mem.base) == 'rip':
            tgt = ins.address + ins.size + ops[1].mem.disp
            sname = self.img.cstr(tgt, 120)
            if sname and sname.endswith('.png'):
                st.note.append(('png', sname))
        return super().exec(st, ins, mn, ops, nxt)
emu = E(img, max_states=600, max_steps=30000)
res = {}
t0 = time.time()
for sid, info in classes.items():
    oid = int(sid)
    if only and oid not in only: continue
    done, ab = emu.run(0x2acb40, {'rcx': ('obj', 0), 'rdx': None, 'rsp': None, 'rbp': None}, oid)
    sites = collections.Counter(); w = collections.defaultdict(collections.Counter)
    for st in done:
        for n in st.note:
            if n[0] == 'site': sites[hex(n[1])] += 1
        last = {}
        for (a, val, size, isf, addr) in st.writes:
            if a[0] == 'obj' and a[1] in WATCH:
                if isinstance(val, int) and a[1] != 0x3a0: val = round(i2f(val), 4)
                elif isinstance(val, float): val = round(val, 4)
                last[hex(a[1])] = str(val)
        for k, v in last.items(): w[k][v] += 1
    pngs = collections.Counter()
    for st in done:
        for n in set(x for x in st.note if x[0] == 'png'):
            pngs[n[1]] += 1
    res[oid] = {'paths': len(done), 'aborted': ab, 'sites': dict(sites), 'writes': {k: dict(v) for k, v in w.items()},
                'child_pngs': {k: v for k, v in pngs.items()}}
json.dump(res, open(out, 'w'), indent=0)
print('time', time.time() - t0)
hit = {k: v for k, v in res.items() if v['sites'] or v['writes']}
print('ids with writes', len(hit))
for k, v in list(hit.items())[:60]: print(k, v)
print('aborted ids', sum(1 for v in res.values() if v['aborted']))
