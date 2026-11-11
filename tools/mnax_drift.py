#!/usr/bin/env python3
"""Report where the linked Maniax image leaves the layout the split config expects.

    python tools/mnax_drift.py

Lists each object whose section address in build/mnax/SLPM_654.62.map stops
agreeing with its subsegment start in config/mnax/SLPM_654.62.yaml, then the
first byte ranges where the built image differs from the retail executable."""
import re
exp={}
for line in open('config/mnax/SLPM_654.62.yaml'):
    m=re.match(r"\s*- \[(0x[0-9A-Fa-f]+), ([.\w]+), ([\w/]+)\]",line)
    if m: exp.setdefault(m.group(3),{})[m.group(2).lstrip('.')]=int(m.group(1),16)+0xff000
t=open('build/mnax/SLPM_654.62.map').read()
prev=0;n=0
for m in re.finditer(r"^ \.(rodata|data|sdata|lit4|text)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) build/mnax/(?:src|asm)/mnax/(?:data/)?(\S+?)(?:\.(?:rodata|data|sdata|lit4))?\.o$",t,re.M):
    sec,addr,size,name=m.group(1),int(m.group(2),16),int(m.group(3),16),m.group(4)
    if not addr or not size: continue
    e=exp.get(name,{}); want=e.get('c',e.get('asm')) if sec=='text' else e.get(sec)
    d=addr-want if want else None
    if d!=prev and d is not None:
        print(f"{sec:6s} {name:40s} at {addr:08X} want {want:08X} drift {d:+d} size {size:X}"); prev=d; n+=1
        if n>25: break
a=open('build/mnax/SLPM_654.62','rb').read(); b=open('orig/mnax/SLPM_654.62','rb').read()
n=min(len(a),len(b)); runs=[]; i=0
while i<n:
    if a[i]!=b[i]:
        j=i
        while j<n and a[j:j+32]!=b[j:j+32]: j+=1
        runs.append((i,j)); i=j
    else: i+=1
print(len(a),len(b),len(runs),'differing runs,',sum(j-i for i,j in runs),'bytes')
for i,j in runs[:14]: print(f"  0x{i:06X}..0x{j:06X} vram 0x{i+0xff000:08X} len {j-i}")
