"""Static extraction of ObjectToolbox::init id->frame map from GeometryDash.exe (read-only)."""
import sys, pefile, capstone, re, json, collections
exe, fstart, fend, out = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3],16), sys.argv[4]
pe = pefile.PE(exe, fast_load=True)
data = open(exe,'rb').read()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.skipdata = True
off = pe.get_offset_from_rva(fstart)
code = data[off:off+(fend-fstart)]
def cstr(rva):
    try: o = pe.get_offset_from_rva(rva)
    except Exception: return None
    e = data.find(b'\0', o, o+200)
    if e<0: return None
    try: return data[o:e].decode('ascii')
    except: return None
riprx = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
regs = {}; slots = {}
calls = collections.Counter()
result = {}; order = []; anomalies = []
pending = None  # (key, base)
REG64 = {'eax':'rax','ebx':'rbx','ecx':'rcx','edx':'rdx','esi':'rsi','edi':'rdi','r8d':'r8','r9d':'r9','r10d':'r10','r11d':'r11','r12d':'r12','r13d':'r13','r14d':'r14','r15d':'r15','ebp':'rbp'}
def norm(r): return REG64.get(r, r)
def val(opstr):
    opstr=opstr.strip()
    if opstr.startswith('0x') or opstr.lstrip('-').isdigit(): return int(opstr,0)
    return regs.get(norm(opstr))
for ins in md.disasm(code, fstart):
    mn, ops = ins.mnemonic, ins.op_str
    ript = None
    m = riprx.search(ops)
    if m:
        d = int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
        ript = ins.address+ins.size+d
    if mn == 'mov' and ops.startswith('dword ptr ['):
        dst, src = ops.split('], ',1); dst = dst[len('dword ptr '):]+']'
        slots[dst] = val(src)
    elif mn == 'mov' and ',' in ops and not ops.startswith(('qword','byte','word','dword')):
        dst, src = [x.strip() for x in ops.split(',',1)]
        if src.startswith('dword ptr ['):
            regs[norm(dst)] = slots.get(src[len('dword ptr '):])
        elif '[' in src:
            regs[norm(dst)] = None
        else:
            regs[norm(dst)] = val(src) if not src in ('rax',) else regs.get('rax')
            if src in regs: regs[norm(dst)] = regs[src]
    elif mn == 'lea':
        dst, src = [x.strip() for x in ops.split(',',1)]
        if ript is not None:
            regs[norm(dst)] = ('str', cstr(ript), ript)
        else:
            regs[norm(dst)] = ('addr', src)
    elif mn == 'call':
        tgt = ops
        calls[tgt]+=1
        if tgt == '0x2d6130':
            a = regs.get('rdx')
            key = slots.get(a[1].replace('[','dword ptr [')[len('dword ptr '):]) if isinstance(a,tuple) and a[0]=='addr' else (a if isinstance(a,int) else None)
            if key is None: anomalies.append(('nokey', hex(ins.address), a))
            if pending is not None: anomalies.append(('unassigned', hex(ins.address), pending))
            pending = key
            regs['rax'] = ('mapref', key)
        elif tgt == '0x40580':
            rc = regs.get('rcx'); rd = regs.get('rdx')
            if isinstance(rc,tuple) and rc[0]=='mapref' and isinstance(rd,tuple) and rd[0]=='str':
                k = rc[1]
                if k in result and result[k]!=rd[1]: anomalies.append(('override', k, result[k], rd[1]))
                result[k] = rd[1]; order.append(k); pending=None
            else:
                anomalies.append(('assign?', hex(ins.address), rc, rd))
            regs['rax']=None
        else:
            if pending is not None: anomalies.append(('othercall', hex(ins.address), tgt, pending))
            regs['rax']=None
        for r in ('rcx','rdx','r8','r9','r10','r11'): regs.pop(r,None)
print('entries', len(result), 'assign ops', len(order))
print('calls', calls.most_common(10))
print('anomalies', len(anomalies))
for a in anomalies[:40]: print(a)
json.dump({str(k):v for k,v in sorted(result.items(), key=lambda kv: kv[0] if isinstance(kv[0],int) else -1)}, open(out,'w'), indent=1)
