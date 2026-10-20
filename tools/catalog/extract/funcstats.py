import sys, re, collections
cur=None; stats={}
for line in open(sys.argv[1]):
    if line.startswith('== func'):
        p=line.split(); cur=(p[2],p[3]); stats[cur]=collections.Counter(); continue
    if '0x40c]' in line: stats[cur]['id40c']+=1
    if 'jmp rcx' in line or 'jmp rax' in line or 'jmp rdx' in line or 'jmp r8' in line: stats[cur]['jmpreg']+=1
    if re.search(r'movzx e.., byte ptr \[r.. \+ r.. \+ 0x', line): stats[cur]['bytetab']+=1
    stats[cur]['n']+=1
rows=sorted(stats.items(), key=lambda kv:-kv[1]['jmpreg'])
for k,v in rows[:40]:
    print(k, dict(v))
