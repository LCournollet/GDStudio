import sys, pefile, struct
pe=pefile.PE(sys.argv[1], fast_load=True); data=open(sys.argv[1],'rb').read()
def rd(rva, fmt): return struct.unpack_from(fmt, data, pe.get_offset_from_rva(rva))[0]
bt, dt, base = int(sys.argv[2],16), int(sys.argv[3],16), int(sys.argv[4])
for i in map(int, sys.argv[5].split(',')):
    b=rd(bt+i-base,'<B'); t=rd(dt+b*4,'<I'); print(i, b, hex(t))
