import sys, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
print(hex(pe.OPTIONAL_HEADER.ImageBase), pe.FILE_HEADER.Machine)
for s in pe.sections:
    print(s.Name.rstrip(b'\0'), hex(s.VirtualAddress), hex(s.Misc_VirtualSize), hex(s.PointerToRawData), hex(s.SizeOfRawData))
data = open(sys.argv[1],'rb').read()
for needle in [b'square_01_001.png', b'spike_01_001.png', b'ObjectToolbox', b'2.209', b'2.208', b'2.207']:
    i = data.find(needle); cnt = data.count(needle)
    print(needle, hex(i), cnt, data[max(0,i-40):i+60] if i>=0 else None)
