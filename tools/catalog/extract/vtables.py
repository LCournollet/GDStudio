import sys, pefile, struct, re, json
exe=sys.argv[1]
pe=pefile.PE(exe, fast_load=True); data=open(exe,'rb').read()
IB=pe.OPTIONAL_HEADER.ImageBase
rd=[s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
def q(rva): return struct.unpack_from('<Q', data, pe.get_offset_from_rva(rva))[0]
def d(rva): return struct.unpack_from('<I', data, pe.get_offset_from_rva(rva))[0]
# find all COLs: scan rdata for qwords that point to COL with sig 1 and type descriptor name containing GameObject/Object
out={}
r0=rd.PointerToRawData; n=rd.SizeOfRawData
for off in range(0, n-8, 8):
    v=struct.unpack_from('<Q', data, r0+off)[0]
    if not (IB+rd.VirtualAddress <= v < IB+rd.VirtualAddress+n): continue
    col=v-IB
    try:
        if d(col)!=1 or d(col+4)!=0: continue
        td=d(col+12); o=pe.get_offset_from_rva(td+16); e=data.find(b'\0',o,o+200)
        name=data[o:e].decode()
    except Exception: continue
    if not name.startswith('.?AV'): continue
    vt=rd.VirtualAddress+off+8
    # first entry must point into .text
    f0=q(vt)-IB
    if not (0x1000<=f0<0x6ce000): continue
    nm=name[4:-2]
    if 'Object' in nm or 'Trigger' in nm or nm in ('GameObject',):
        try: s139=q(vt+139*8)-IB; s140=q(vt+140*8)-IB
        except Exception: continue
        out.setdefault(nm, []).append((hex(vt), hex(s139), hex(s140)))
for k,v in sorted(out.items()): print(k, v)
json.dump(out, open(sys.argv[2],'w'), indent=1)
