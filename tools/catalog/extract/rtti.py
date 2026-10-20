import sys, pefile, struct, re
exe=sys.argv[1]
pe=pefile.PE(exe, fast_load=True); data=open(exe,'rb').read()
IB=pe.OPTIONAL_HEADER.ImageBase
rd=[s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
r0=rd.PointerToRawData; rv=rd.VirtualAddress; rbytes=data[r0:r0+rd.SizeOfRawData]
def q(rva):
    return struct.unpack_from('<Q', data, pe.get_offset_from_rva(rva))[0]
def d(rva): return struct.unpack_from('<I', data, pe.get_offset_from_rva(rva))[0]
def name_of_col(colva):
    try:
        col=colva-IB
        if d(col)!=1: return None
        td=d(col+12)
        o=pe.get_offset_from_rva(td+16); e=data.find(b'\0',o,o+200)
        return data[o:e].decode()
    except Exception: return None
def vtable_of(func_rva):
    target=struct.pack('<Q', IB+func_rva); res=[]
    for m in re.finditer(re.escape(target), rbytes):
        if m.start()%8: continue
        rva=rv+m.start()
        # walk back to find COL
        p=rva
        for k in range(0,600):
            v=q(p-8)
            n=name_of_col(v)
            if n: res.append((hex(rva), hex(p), n, (rva-p)//8)); break
            p-=8
    return res
for f in sys.argv[2:]:
    print(f, vtable_of(int(f,16)))
