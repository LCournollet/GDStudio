import sys, pefile, struct, pickle, bisect
exe=sys.argv[1]
pe = pefile.PE(exe, fast_load=True)
data=open(exe,'rb').read()
p=[s for s in pe.sections if s.Name.startswith(b'.pdata')][0]
raw=data[p.PointerToRawData:p.PointerToRawData+p.Misc_VirtualSize]
funcs=[struct.unpack_from('<III',raw,i) for i in range(0,len(raw)-11,12)]
funcs=[f for f in funcs if f[0]]
funcs.sort()
pickle.dump(funcs, open(sys.argv[2],'wb'))
refs=pickle.load(open(sys.argv[3],'rb'))
starts=[f[0] for f in funcs]
import collections
c=collections.Counter()
for a,t,s in refs:
    i=bisect.bisect_right(starts,a)-1
    c[funcs[i][0],funcs[i][1]]+=1
for (b,e),v in sorted(c.items()):
    if v>20: print(hex(b),hex(e),v)
