import sys, struct, os, collections
SIG=b'\x89PNG\r\n\x1a\n'
def scan(path):
    d=open(path,'rb').read()
    res=[]; off=0
    while True:
        i=d.find(SIG,off)
        if i<0: break
        off=i+8
        if i+8+25>len(d): break
        try:
            ln=struct.unpack_from('>I',d,i+8)[0]
            typ=d[i+12:i+16]
            if typ==b'IHDR' and ln==13:
                w,h,bd,ct=struct.unpack_from('>IIBB',d,i+16)
                res.append((w,h,bd,ct,i))
        except Exception: pass
    return res
for p in sys.argv[1:]:
    r=scan(p)
    if not r:
        print(f'{os.path.basename(p)}: no PNG'); continue
    ws=collections.Counter(x[0] for x in r); hs=sorted(set(x[1] for x in r))
    print(f'== {os.path.basename(p)}  ({os.path.getsize(p):,} B)  PNG数={len(r)}')
    print(f'   width 分布: {sorted(ws.items())[-8:]}')
    print(f'   height 范围: {min(x[1] for x in r)} .. {max(x[1] for x in r)}')
    top=sorted(r,key=lambda x:-(x[0]*x[1]))[:8]
    print('   最大几张: ' + ', '.join(f'{w}x{h}' for w,h,b,c,o in top))
    print('   色深/类型: ' + str(sorted(set((x[2],x[3]) for x in r))))
