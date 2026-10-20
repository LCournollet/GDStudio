"""Resolve which C++ class GameObject::createWithKey (0x288930) instantiates for each object id.
Usage: python resolve_classes.py <GeometryDash.exe> <vtables.json> <raw_toolbox.json> <out.json>
"""
import sys, json, re, collections, struct
sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from emu import Image, Emu
from capstone import x86_const as X

exe, vtj, tbj, out = sys.argv[1:5]
img = Image(exe)
vt = json.load(open(vtj))
VT = {}
for name, lst in vt.items():
    for v, s139, s140 in lst:
        VT[int(v, 16)] = name
ids = sorted(int(k) for k in json.load(open(tbj)))

# function -> classes whose vtable it lea's (one level), to classify create helpers
def func_vt_leas(start, maxlen=0x800):
    found = []
    o = img.off(start)
    for ins in img.md.disasm(img.data[o:o + maxlen], start):
        if ins.mnemonic == 'lea' and ins.operands[1].type == X.X86_OP_MEM and ins.reg_name(ins.operands[1].mem.base) == 'rip':
            t = ins.address + ins.size + ins.operands[1].mem.disp
            if t in VT:
                found.append(VT[t])
        if ins.mnemonic in ('ret', 'int3'):
            break
    return found

class WatchEmu(Emu):
    def exec(self, st, ins, mn, ops, nxt):
        if mn == 'lea' and ops[1].type == X.X86_OP_MEM and ins.reg_name(ops[1].mem.base) == 'rip':
            t = ins.address + ins.size + ops[1].mem.disp
            if t in VT:
                st.note.append(('vt', VT[t], ins.address))
        if mn == 'mov' and ops[0].type == X.X86_OP_MEM and ops[1].type == X.X86_OP_IMM:
            m = ops[0].mem
            if m.disp in (0x3a0, 0x3a8) and m.base and ins.reg_name(m.base) == 'rdi':
                st.note.append(('type', m.disp, ops[1].imm, ins.address))
        return super().exec(st, ins, mn, ops, nxt)

emu = WatchEmu(img, max_states=400, max_steps=20000, stop_at={0x2893e4, 0x289405})
callee_cache = {}
def direct_calls(start, maxlen=0x400):
    o = img.off(start); res = []
    for ins in img.md.disasm(img.data[o:o + maxlen], start):
        if ins.mnemonic == 'call' and ins.operands[0].type == X.X86_OP_IMM:
            res.append(ins.operands[0].imm)
        if ins.mnemonic in ('ret', 'int3'):
            break
    return res

def callee_class(t, depth=0):
    if t not in callee_cache:
        c = func_vt_leas(t)
        r = c[-1] if c else None
        if r is None and depth < 1:
            for t2 in direct_calls(t):
                r = callee_class(t2, depth + 1)
                if r: break
        callee_cache[t] = r
    return callee_cache[t]

result = {}
for oid in ids:
    regs = {'r14': oid, 'rcx': None, 'rbx': ('framestr',), 'rsp': None, 'rbp': None, 'r15': 0xffffffffffffffff}
    done, aborted = emu.run(0x288992, regs, oid)
    classes = collections.Counter(); types = set()
    for st in done:
        vts = [n for n in st.note if n[0] == 'vt']
        cls = None
        if vts:
            cls = vts[-1][1]
        else:
            for (addr, tgt, rcx, edx, rdx) in reversed(st.calls):
                if isinstance(tgt, int):
                    c = callee_class(tgt)
                    if c:
                        cls = c; break
        classes[cls] += 1
        for n in st.note:
            if n[0] == 'type':
                types.add((n[1], n[2]))
    result[oid] = {'classes': dict(classes), 'aborted': aborted, 'init_type_writes': sorted(types)}

json.dump(result, open(out, 'w'), indent=0)
cnt = collections.Counter()
amb = []
for oid, r in result.items():
    cs = [c for c in r['classes'] if c]
    if len(cs) == 1:
        cnt[cs[0]] += 1
    else:
        amb.append((oid, r['classes'], r['aborted']))
print(cnt.most_common())
print('ambiguous/unresolved', len(amb))
for a in amb[:40]:
    print(a)
