import sys, pefile, capstone, re, pickle
exe=sys.argv[1]; out=sys.argv[3]
pe=pefile.PE(exe, fast_load=True); data=open(exe,'rb').read()
funcs=pickle.load(open(sys.argv[2],'rb'))
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
riprx=re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
strcache={}
def cstr(rva):
    if rva in strcache: return strcache[rva]
    r=None
    try:
        o=pe.get_offset_from_rva(rva); e=data.find(b'\0',o,o+100)
        if e>o:
            s=data[o:e]
            if all(32<=c<127 for c in s) and len(s)>=3: r=s.decode()
    except Exception: pass
    strcache[rva]=r; return r
with open(out,'w') as f:
    for b,e,u in funcs:
        if e-b > 0x200000: continue
        o=pe.get_offset_from_rva(b)
        f.write(f'== func {b:08x} {e:08x}\n')
        for ins in md.disasm(data[o:o+(e-b)], b):
            ops=ins.op_str; extra=''
            m=riprx.search(ops)
            if m:
                t=ins.address+ins.size+int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
                s=cstr(t)
                extra=f' ; {t:x}' + (f' {s!r}' if s else '')
            f.write(f'{ins.address:08x}: {ins.mnemonic} {ops}{extra}\n')
