import sys, pefile, capstone
exe = sys.argv[1]; start = int(sys.argv[2],16); n = int(sys.argv[3])
pe = pefile.PE(exe, fast_load=True)
data = open(exe,'rb').read()
IB = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
off = pe.get_offset_from_rva(start)
def cstr(rva):
    try: o = pe.get_offset_from_rva(rva)
    except Exception: return None
    e = data.find(b'\0', o, o+120)
    if e<0: return None
    try:
        s=data[o:e].decode('ascii')
        return s if s.isprintable() and len(s)>1 else None
    except: return None
for k,ins in enumerate(md.disasm(data[off:off+n*8], start)):
    if k>=n: break
    extra=''
    if 'rip' in ins.op_str:
        import re
        m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', ins.op_str)
        if m:
            d = int(m.group(2),16)*(1 if m.group(1)=='+' else -1)
            t = ins.address+ins.size+d
            s = cstr(t)
            extra = f'  ; {hex(t)} {s!r}' if s else f'  ; {hex(t)}'
    print(f'{ins.address:08x}: {ins.mnemonic} {ins.op_str}{extra}')
