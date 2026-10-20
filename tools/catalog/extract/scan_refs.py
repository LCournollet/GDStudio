import sys, pefile, re, struct, collections
exe = sys.argv[1]
pe = pefile.PE(exe, fast_load=True)
data = open(exe,'rb').read()
IB = pe.OPTIONAL_HEADER.ImageBase
secs = {s.Name.rstrip(b'\0').decode(): s for s in pe.sections}
text = secs['.text']; rdata = secs['.rdata']
tdata = data[text.PointerToRawData:text.PointerToRawData+text.Misc_VirtualSize]
rva2off = lambda rva: pe.get_offset_from_rva(rva)
def cstr(rva):
    try: o = rva2off(rva)
    except Exception: return None
    e = data.find(b'\0', o, o+200)
    if e < 0: return None
    s = data[o:e]
    try: return s.decode('ascii')
    except: return None
# find lea reg,[rip+disp]
refs = []
for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', tdata):
    i = m.start()
    disp = struct.unpack_from('<i', tdata, i+3)[0]
    rva = text.VirtualAddress + i + 7 + disp
    if rdata.VirtualAddress <= rva < rdata.VirtualAddress + rdata.Misc_VirtualSize:
        s = cstr(rva)
        if s and s.endswith('.png') and len(s) > 4:
            refs.append((text.VirtualAddress+i, rva, s))
print('png lea refs', len(refs))
# cluster by 64KB windows
c = collections.Counter((r[0]>>16) for r in refs)
for k,v in c.most_common(15): print(hex(k<<16), v)
import pickle
pickle.dump(refs, open(sys.argv[2],'wb'))
sq = [r for r in refs if r[2]=='square_01_001.png']
for r in sq: print(hex(r[0]), hex(r[1]))
