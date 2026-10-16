#!/usr/bin/env python3
"""从 libIMDB.so 提取 App 的 dlopen 目标表，为所有缺失库生成桩库。
用法: python3 gen_all_stubs.py <libIMDB.so> <已有lib目录> <输出源码目录>
"""
import re, os, sys
src, havedir, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
d=open(src,'rb').read()
strs=[]; i=0
while i < len(d):
    j=d.find(b'\0', i)
    if j<0: break
    s=d[i:j]
    if 4<=len(s)<=64 and all(32<=c<127 for c in s): strs.append(s.decode())
    i=j+1
libs=[]
for idx,s in enumerate(strs):
    if re.fullmatch(r'lib[a-zA-Z0-9_]+\.so', s):
        syms=[]
        for k in range(idx+1, min(idx+30, len(strs))):
            t=strs[k]
            if re.fullmatch(r'lib[a-zA-Z0-9_]+\.so', t): break
            if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{2,}', t): syms.append(t)
        libs.append((s, syms))
have=set(os.listdir(havedir))
os.makedirs(outdir, exist_ok=True)
n=0
for name, syms in libs:
    if name in have or not syms: continue
    base=name[3:-3]
    body="\n".join(f"int {x}(void) {{ return 0; }}" for x in syms)
    open(os.path.join(outdir, base+'.c'),'w').write(
        f"/* QEMU stub for {name} */\n{body}\n")
    n+=1
print(f"generated {n} stubs")
