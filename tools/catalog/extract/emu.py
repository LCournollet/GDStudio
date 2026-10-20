"""Tiny path-exploring x86-64 symbolic emulator, specialised for GD's
GameObject::createWithKey / customSetup switch code.  Read-only on the exe.

Values: int (concrete, unsigned) | tuple (symbolic) | None (unknown)
  ('obj', off)          pointer to the GameObject + off
  ('optr', off)         qword loaded from obj+off (pointer to a sub-struct)
  ('fld', off)          unknown dword loaded from obj+off
"""
import struct, pefile, capstone
from capstone import x86_const as X

MASK = {1: 0xff, 2: 0xffff, 4: 0xffffffff, 8: 0xffffffffffffffff}


def _build_regmap():
    m = {}
    for b in 'abcd':
        full = f'r{b}x'
        m[full] = (full, 8, 0); m[f'e{b}x'] = (full, 4, 0); m[f'{b}x'] = (full, 2, 0)
        m[f'{b}l'] = (full, 1, 0); m[f'{b}h'] = (full, 1, 8)
    for b in ('si', 'di', 'bp', 'sp'):
        full = 'r' + b
        m[full] = (full, 8, 0); m['e' + b] = (full, 4, 0); m[b] = (full, 2, 0); m[b + 'l'] = (full, 1, 0)
    for i in range(8, 16):
        full = f'r{i}'
        m[full] = (full, 8, 0); m[full + 'd'] = (full, 4, 0); m[full + 'w'] = (full, 2, 0); m[full + 'b'] = (full, 1, 0)
    return m


REGMAP = _build_regmap()
VOLATILE = ['rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11']
NONVOL = ['rbx', 'rbp', 'rsi', 'rdi', 'r12', 'r13', 'r14', 'r15', 'rsp']


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.data = open(path, 'rb').read()
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self.cache = {}
        self.secs = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData) for s in self.pe.sections]

    def off(self, rva):
        for va, end, raw, rsz in self.secs:
            if va <= rva < end and rva - va < rsz:
                return raw + rva - va
        return None

    def read(self, rva, size):
        o = self.off(rva)
        if o is None:
            return None
        return int.from_bytes(self.data[o:o + size], 'little')

    def readf(self, rva):
        o = self.off(rva)
        if o is None:
            return None
        return struct.unpack_from('<f', self.data, o)[0]

    def cstr(self, rva, maxlen=200):
        o = self.off(rva)
        if o is None:
            return None
        e = self.data.find(b'\0', o, o + maxlen)
        if e < 0:
            return None
        try:
            return self.data[o:e].decode('ascii')
        except UnicodeDecodeError:
            return None

    def ins(self, rva):
        r = self.cache.get(rva)
        if r is None:
            o = self.off(rva)
            r = next(self.md.disasm(self.data[o:o + 16], rva), None)
            self.cache[rva] = r
        return r


def f2i(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def i2f(i):
    return struct.unpack('<f', struct.pack('<I', i & 0xffffffff))[0]


class State:
    __slots__ = ('pc', 'regs', 'xmm', 'flags', 'mem', 'stack', 'writes', 'calls', 'steps', 'visits', 'pushes', 'note')

    def copy(self):
        s = State.__new__(State)
        s.pc = self.pc
        s.regs = dict(self.regs)
        s.xmm = dict(self.xmm)
        s.flags = self.flags
        s.mem = dict(self.mem)
        s.stack = list(self.stack)
        s.writes = list(self.writes)
        s.calls = list(self.calls)
        s.steps = self.steps
        s.visits = dict(self.visits)
        s.pushes = [list(p) for p in self.pushes]
        s.note = list(self.note)
        return s


class Emu:
    def __init__(self, img, follow=(), tailcall_targets=(), nonnull_optr=(0x450,), max_states=3000, max_steps=40000,
                 stop_at=(), skip_calls=()):
        self.img = img
        self.follow = set(follow)
        self.tail = set(tailcall_targets)
        self.nonnull = set(nonnull_optr)
        self.max_states = max_states
        self.max_steps = max_steps
        self.stop_at = set(stop_at)
        self.skip_calls = set(skip_calls)

    # ---------------- register helpers
    def rget(self, st, name):
        if name.startswith('xmm'):
            return st.xmm.get(name)
        full, size, shift = REGMAP[name]
        v = st.regs.get(full)
        if size == 8:
            return v
        if isinstance(v, int):
            return (v >> shift) & MASK[size]
        if isinstance(v, tuple) and size == 4 and v[0] in ('fld',):
            return v
        return None if not isinstance(v, tuple) else (v if size == 4 else None)

    def rset(self, st, name, val):
        if name.startswith('xmm'):
            st.xmm[name] = val
            return
        full, size, shift = REGMAP[name]
        if size in (8, 4):
            if isinstance(val, int):
                val &= MASK[size]
            st.regs[full] = val
        else:
            old = st.regs.get(full)
            if isinstance(old, int) and isinstance(val, int):
                m = MASK[size] << shift
                st.regs[full] = (old & ~m) | ((val & MASK[size]) << shift)
            else:
                st.regs[full] = None

    # ---------------- memory
    def addr(self, st, ins, op):
        mem = op.mem
        base = ins.reg_name(mem.base) if mem.base else None
        idx = ins.reg_name(mem.index) if mem.index else None
        disp = mem.disp
        if base == 'rip':
            return ins.address + ins.size + disp
        if base in ('rsp', 'rbp'):
            if idx:
                return None
            return ('stack', base, disp, len(st.stack))
        bv = self.rget(st, base) if base else 0
        iv = self.rget(st, idx) if idx else 0
        if idx and not isinstance(iv, int):
            return None
        if isinstance(bv, int):
            return (bv + iv * mem.scale + disp) & MASK[8]
        if isinstance(bv, tuple):
            if bv[0] == 'obj' and not idx:
                return ('obj', bv[1] + disp)
            if bv[0] == 'optr' and not idx:
                return ('sub', bv[1], disp)
        return None

    def mread(self, st, a, size):
        if a is None:
            return None
        if isinstance(a, int):
            v = self.img.read(a, size)
            return v
        if a in st.mem:
            v, sz = st.mem[a]
            if sz == size or not isinstance(v, int):
                return v
            if isinstance(v, int) and sz > size:
                return v & MASK[size]
            return None
        if a[0] == 'obj':
            if a[1] == self.id_off and size == 4:
                return self.cur_id
            if size == 8:
                return ('optr', a[1])
            return ('fld', a[1])
        return None

    def mwrite(self, st, a, val, size, ins, is_float=False):
        if a is None or isinstance(a, int):
            return
        st.mem[a] = (val, size)
        if a[0] in ('obj', 'sub'):
            st.writes.append((a, val, size, is_float, ins.address))

    # ---------------- operand access
    def opget(self, st, ins, op, size=None):
        if op.type == X.X86_OP_REG:
            return self.rget(st, ins.reg_name(op.reg))
        if op.type == X.X86_OP_IMM:
            v = op.imm
            sz = size or op.size
            return v & MASK.get(sz, MASK[8])
        if op.type == X.X86_OP_MEM:
            return self.mread(st, self.addr(st, ins, op), size or op.size)

    def opset(self, st, ins, op, val, is_float=False):
        if op.type == X.X86_OP_REG:
            self.rset(st, ins.reg_name(op.reg), val)
        elif op.type == X.X86_OP_MEM:
            self.mwrite(st, self.addr(st, ins, op), val, op.size, ins, is_float)

    # ---------------- flags
    @staticmethod
    def _sx(v, bits):
        return v - (1 << bits) if v & (1 << (bits - 1)) else v

    def cond(self, st, cc):
        f = st.flags
        if f is None:
            return None
        kind = f[0]
        if kind == 'unk':
            return None
        if kind == 'nonnull':
            ZF, SF, CF, OF = False, None, False, False
        elif kind == 'null?':
            return None
        else:
            _, a, b, size = f
            if not (isinstance(a, int) and isinstance(b, int)):
                return None
            bits = size * 8
            a &= MASK[size]; b &= MASK[size]
            if kind == 'cmp':
                r = (a - b) & MASK[size]
                ZF = r == 0; CF = a < b; SF = bool(r >> (bits - 1))
                sr = self._sx(a, bits) - self._sx(b, bits)
                OF = not (-(1 << (bits - 1)) <= sr < (1 << (bits - 1)))
            elif kind == 'test':
                r = a & b
                ZF = r == 0; SF = bool(r >> (bits - 1)); CF = OF = False
            elif kind == 'add':
                r = (a + b) & MASK[size]
                ZF = r == 0; CF = a + b > MASK[size]; SF = bool(r >> (bits - 1))
                sr = self._sx(a, bits) + self._sx(b, bits)
                OF = not (-(1 << (bits - 1)) <= sr < (1 << (bits - 1)))
            elif kind == 'fcmp':
                ZF = a == b; CF = a < b; SF = False; OF = False
            elif kind == 'bt':
                CF = bool((a >> (b % bits)) & 1); ZF = SF = OF = None
            else:
                return None
        t = {
            'e': lambda: ZF, 'z': lambda: ZF, 'ne': lambda: not ZF, 'nz': lambda: not ZF,
            'g': lambda: (not ZF) and SF == OF, 'ge': lambda: SF == OF, 'l': lambda: SF != OF,
            'le': lambda: ZF or SF != OF, 'a': lambda: (not CF) and (not ZF), 'ae': lambda: not CF,
            'nb': lambda: not CF, 'b': lambda: CF, 'c': lambda: CF, 'nc': lambda: not CF,
            'be': lambda: CF or ZF, 's': lambda: SF, 'ns': lambda: not SF,
        }.get(cc)
        if t is None:
            return None
        try:
            r = t()
        except TypeError:
            return None
        if r is None:
            return None
        return bool(r)

    # ---------------- main loop
    def run(self, entry, init_regs, cur_id, id_off=0x40c, init_mem=None):
        import collections as _c
        self.reasons = _c.Counter()
        self.cur_id = cur_id
        self.id_off = id_off
        s = State()
        s.pc = entry; s.regs = dict(init_regs); s.xmm = {}; s.flags = None
        s.mem = dict(init_mem or {}); s.stack = []; s.writes = []; s.calls = []; s.steps = 0; s.visits = {}
        s.pushes = [[]]; s.note = []
        work = [s]; done = []; nstates = 1; aborted = 0
        while work:
            st = work.pop()
            res = self.step_path(st)
            if res is None:
                continue
            for r in res:
                if r == 'abort':
                    aborted += 1
                elif r[0] == 'done':
                    done.append(r[1])
                elif r[0] == 'fork':
                    nstates += 1
                    if nstates > self.max_states:
                        self.reasons['maxstates'] += 1
                        aborted += 1
                        continue
                    work.append(r[1])
        return done, aborted

    def step_path(self, st):
        img = self.img
        while True:
            if st.pc in self.stop_at:
                return [('done', st)]
            st.steps += 1
            if st.steps > self.max_steps:
                self.reasons['steps'] += 1
                return ['abort']
            v = st.visits.get(st.pc, 0) + 1
            st.visits[st.pc] = v
            if v > 40:
                self.reasons['loop@%x' % st.pc] += 1
                return ['abort']
            ins = img.ins(st.pc)
            if ins is None:
                st.note.append(('badpc', st.pc))
                return ['abort']
            nxt = ins.address + ins.size
            mn = ins.mnemonic
            ops = ins.operands
            try:
                r = self.exec(st, ins, mn, ops, nxt)
            except Exception as e:  # pragma: no cover
                self.reasons['exc %s %x %s' % (mn, ins.address, e)] += 1
                st.note.append(('exc', hex(ins.address), mn, str(e)))
                return ['abort']
            if r is None:
                st.pc = nxt
                continue
            if r == 'goto':
                continue
            return r

    def branch(self, st, taken_pc, fall_pc, c):
        if c is True:
            st.pc = taken_pc; return 'goto'
        if c is False:
            st.pc = fall_pc; return 'goto'
        other = st.copy(); other.pc = fall_pc
        st.pc = taken_pc
        return [('fork', other), ('fork', st)]

    def do_call(self, st, ins, target, nxt):
        if isinstance(target, int) and target in self.follow:
            st.stack.append((nxt, {r: st.regs.get(r) for r in NONVOL}))
            st.pushes.append([])
            st.pc = target
            return 'jumped'
        st.calls.append((ins.address, target, self.rget(st, 'rcx'), self.rget(st, 'edx'), self.rget(st, 'rdx')))
        if isinstance(target, int) and target in self.skip_calls:
            pass
        # compiled code never relies on a register the callee may clobber, so only the
        # return registers need to be invalidated
        st.regs['rax'] = None
        st.xmm['xmm0'] = None
        st.flags = None
        return None

    def exec(self, st, ins, mn, ops, nxt):
        # control flow ----------------------------------------------------
        if mn == 'ret':
            if st.stack:
                ra, saved = st.stack.pop(); st.pushes.pop()
                st.pc = ra; st.regs.update(saved)
                return 'goto'
            return [('done', st)]
        if mn == 'int3' or mn == 'ud2':
            return ['abort']
        if mn == 'jmp':
            op = ops[0]
            if op.type == X.X86_OP_IMM:
                t = op.imm
                if t in self.tail and t not in self.follow:
                    self.do_call(st, ins, t, None)
                    if st.stack:
                        ra, saved = st.stack.pop(); st.pushes.pop()
                        st.pc = ra; st.regs.update(saved); return 'goto'
                    return [('done', st)]
                if t in self.tail and t in self.follow:
                    st.pc = t; return 'goto'
                st.pc = t; return 'goto'
            t = self.opget(st, ins, op)
            if isinstance(t, int):
                st.pc = t; return 'goto'
            st.note.append(('badjmp', hex(ins.address)))
            self.reasons['badjmp@%x' % ins.address] += 1
            return ['abort']
        if mn.startswith('j'):
            cc = mn[1:]
            c = self.cond(st, cc)
            return self.branch(st, ops[0].imm, nxt, c)
        if mn == 'call':
            op = ops[0]
            if op.type == X.X86_OP_IMM:
                t = op.imm
            else:
                t = ('ind', ins.op_str)
            r = self.do_call(st, ins, t, nxt)
            if r == 'jumped':
                return 'goto'
            return None
        # data movement -----------------------------------------------------
        if mn in ('mov', 'movabs'):
            dst, src = ops
            val = self.opget(st, ins, src, dst.size)
            self.opset(st, ins, dst, val)
            return None
        if mn in ('movzx', 'movsx', 'movsxd'):
            dst, src = ops
            val = self.opget(st, ins, src)
            if isinstance(val, int) and mn != 'movzx':
                bits = src.size * 8
                val = self._sx(val, bits) & MASK[dst.size]
            self.opset(st, ins, dst, val)
            return None
        if mn == 'cdqe':
            v = self.rget(st, 'eax')
            self.rset(st, 'rax', (self._sx(v, 32) & MASK[8]) if isinstance(v, int) else v)
            return None
        if mn == 'lea':
            dst, src = ops
            a = self.addr(st, ins, src)
            if isinstance(a, tuple) and a[0] == 'stack':
                a = ('stackaddr',) + a[1:]
            if isinstance(a, int) and dst.size == 4:
                a &= MASK[4]
            self.opset(st, ins, dst, a)
            return None
        if mn == 'push':
            st.pushes[-1].append(self.opget(st, ins, ops[0]))
            return None
        if mn == 'pop':
            v = st.pushes[-1].pop() if st.pushes[-1] else None
            self.opset(st, ins, ops[0], v)
            return None
        if mn == 'xchg':
            a, b = ops
            va, vb = self.opget(st, ins, a), self.opget(st, ins, b)
            self.opset(st, ins, a, vb); self.opset(st, ins, b, va)
            return None
        # arithmetic -------------------------------------------------------
        if mn in ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'test', 'imul', 'shl', 'shr', 'sar', 'sal'):
            if mn == 'imul' and len(ops) != 3 and len(ops) != 2:
                for r in ('rax', 'rdx'):
                    st.regs[r] = None
                st.flags = None
                return None
            if mn == 'imul' and len(ops) == 3:
                dst, a, b = ops
                va, vb = self.opget(st, ins, a), self.opget(st, ins, b, dst.size)
                self.opset(st, ins, dst, (va * vb) & MASK[dst.size] if isinstance(va, int) and isinstance(vb, int) else None)
                st.flags = None
                return None
            dst, src = ops
            size = dst.size
            a = self.opget(st, ins, dst)
            b = self.opget(st, ins, src, size)
            if mn in ('xor',) and dst.type == X.X86_OP_REG and src.type == X.X86_OP_REG and dst.reg == src.reg:
                self.opset(st, ins, dst, 0)
                st.flags = ('test', 0, 0, size)
                return None
            if mn == 'cmp':
                st.flags = ('cmp', a, b, size)
                return None
            if mn == 'test':
                if isinstance(a, tuple) and a == b and a[0] == 'optr':
                    st.flags = ('nonnull',) if a[1] in self.nonnull else ('null?',)
                else:
                    st.flags = ('test', a, b, size)
                return None
            conc = isinstance(a, int) and isinstance(b, int)
            if mn == 'add':
                if isinstance(a, tuple) and a[0] == 'obj' and isinstance(b, int):
                    self.opset(st, ins, dst, ('obj', a[1] + b)); st.flags = None; return None
                r = (a + b) & MASK[size] if conc else None
                st.flags = ('add', a, b, size)
            elif mn == 'sub':
                r = (a - b) & MASK[size] if conc else None
                st.flags = ('cmp', a, b, size)
            elif mn == 'and':
                r = a & b if conc else None
                st.flags = ('test', a, b, size)
            elif mn == 'or':
                r = a | b if conc else None
                st.flags = ('test', r, r, size) if conc else None
            elif mn == 'xor':
                r = a ^ b if conc else None
                st.flags = ('test', r, r, size) if conc else None
            elif mn == 'imul':
                r = (a * b) & MASK[size] if conc else None
                st.flags = None
            elif mn in ('shl', 'sal'):
                r = (a << (b & 63)) & MASK[size] if conc else None
                st.flags = None
            elif mn == 'shr':
                r = (a >> (b & 63)) if conc else None
                st.flags = None
            elif mn == 'sar':
                r = (self._sx(a, size * 8) >> (b & 63)) & MASK[size] if conc else None
                st.flags = None
            self.opset(st, ins, dst, r)
            return None
        if mn in ('inc', 'dec', 'neg', 'not'):
            dst = ops[0]; size = dst.size
            a = self.opget(st, ins, dst)
            if isinstance(a, int):
                r = {'inc': a + 1, 'dec': a - 1, 'neg': -a, 'not': ~a}[mn] & MASK[size]
                st.flags = ('cmp', a, 1, size) if mn == 'dec' else (('add', a, 1, size) if mn == 'inc' else ('test', r, r, size))
            else:
                r = None; st.flags = None
            self.opset(st, ins, dst, r)
            return None
        if mn == 'bt':
            a = self.opget(st, ins, ops[0]); b = self.opget(st, ins, ops[1])
            st.flags = ('bt', a, b, ops[0].size)
            return None
        if mn.startswith('set'):
            c = self.cond(st, mn[3:])
            self.opset(st, ins, ops[0], None if c is None else int(c))
            return None
        if mn.startswith('cmov'):
            c = self.cond(st, mn[4:])
            dst, src = ops
            if c is True:
                self.opset(st, ins, dst, self.opget(st, ins, src))
            elif c is None:
                self.opset(st, ins, dst, None)
            return None
        # floating point -----------------------------------------------------
        if mn in ('movss', 'movd'):
            dst, src = ops
            if src.type == X.X86_OP_MEM:
                a = self.addr(st, ins, src)
                if isinstance(a, int):
                    raw = self.img.read(a, 4)
                    v = i2f(raw) if raw is not None else None
                else:
                    raw = self.mread(st, a, 4)
                    v = i2f(raw) if isinstance(raw, int) else (raw if isinstance(raw, float) else None)
                self.opset(st, ins, dst, v)
            elif dst.type == X.X86_OP_MEM:
                v = self.opget(st, ins, src)
                if mn == 'movd' and isinstance(v, int):
                    v = i2f(v)
                self.opset(st, ins, dst, v, is_float=True)
            else:
                v = self.opget(st, ins, src)
                dn = ins.reg_name(dst.reg)
                if mn == 'movd':
                    if dn.startswith('xmm'):
                        v = i2f(v) if isinstance(v, int) else None
                    else:
                        v = f2i(v) if isinstance(v, float) else None
                self.opset(st, ins, dst, v)
            return None
        if mn in ('movaps', 'movups', 'movdqa', 'movdqu', 'movapd', 'movq', 'movsd'):
            dst, src = ops
            if dst.type == X.X86_OP_REG and src.type == X.X86_OP_REG:
                self.opset(st, ins, dst, self.opget(st, ins, src))
            elif dst.type == X.X86_OP_REG:
                self.opset(st, ins, dst, None)
            else:
                self.mwrite(st, self.addr(st, ins, dst), None, dst.size, ins)
            return None
        if mn in ('mulss', 'addss', 'subss', 'divss', 'maxss', 'minss'):
            dst, src = ops
            a = self.opget(st, ins, dst)
            if src.type == X.X86_OP_MEM:
                ad = self.addr(st, ins, src)
                if isinstance(ad, int):
                    b = self.img.readf(ad)
                else:
                    b = self.mread(st, ad, 4)
                    b = i2f(b) if isinstance(b, int) else (b if isinstance(b, float) else None)
            else:
                b = self.opget(st, ins, src)
            r = None
            if isinstance(a, float) and isinstance(b, float):
                try:
                    r = {'mulss': a * b, 'addss': a + b, 'subss': a - b, 'divss': a / b if b else None,
                         'maxss': max(a, b), 'minss': min(a, b)}[mn]
                except Exception:
                    r = None
            self.opset(st, ins, dst, r)
            return None
        if mn in ('xorps', 'xorpd', 'pxor'):
            dst, src = ops
            if src.type == X.X86_OP_REG and src.reg == dst.reg:
                self.opset(st, ins, dst, 0.0)
                return None
            a = self.opget(st, ins, dst)
            if src.type == X.X86_OP_MEM:
                ad = self.addr(st, ins, src)
                m = self.img.read(ad, 4) if isinstance(ad, int) else None
                if m == 0x80000000 and isinstance(a, float):
                    self.opset(st, ins, dst, -a)
                    return None
            self.opset(st, ins, dst, None)
            return None
        if mn in ('cvtdq2ps', 'cvtsi2ss'):
            dst, src = ops
            v = self.opget(st, ins, src)
            if mn == 'cvtdq2ps' and isinstance(v, float):
                v = f2i(v)
            self.opset(st, ins, dst, float(self._sx(v, 32)) if isinstance(v, int) else None)
            return None
        if mn in ('cvttss2si', 'cvtss2si'):
            dst, src = ops
            v = self.opget(st, ins, src)
            self.opset(st, ins, dst, int(v) & MASK[dst.size] if isinstance(v, float) else None)
            return None
        if mn in ('comiss', 'ucomiss'):
            a = self.opget(st, ins, ops[0])
            src = ops[1]
            if src.type == X.X86_OP_MEM:
                ad = self.addr(st, ins, src)
                b = self.img.readf(ad) if isinstance(ad, int) else None
            else:
                b = self.opget(st, ins, src)
            st.flags = ('fcmp', a, b, 4) if isinstance(a, float) and isinstance(b, float) else None
            return None
        if mn in ('nop', 'endbr64', 'pause', 'prefetchw', 'cpuid') or mn.startswith('prefetch'):
            return None
        # generic fallback: clobber written regs
        try:
            _, wr = ins.regs_access()
            for r in wr:
                n = ins.reg_name(r)
                if n in REGMAP:
                    st.regs[REGMAP[n][0]] = None
                elif n.startswith('xmm'):
                    st.xmm[n] = None
                elif n == 'rflags':
                    st.flags = None
        except Exception:
            pass
        for op in ops:
            if op.type == X.X86_OP_MEM and (op.access & capstone.CS_AC_WRITE):
                self.mwrite(st, self.addr(st, ins, op), None, op.size, ins)
        st.flags = None if mn not in ('lea',) else st.flags
        return None

    def _cont(self, st):
        return [('fork', st)]
