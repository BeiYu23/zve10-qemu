import sys, struct, os, collections
def parse(path):
    d=open(path,'rb').read()
    assert d[:4]==b'UXAC', d[:4]
    ver,count,dstart,h10,h14=struct.unpack_from('<IIIII',d,4)
    print(f'== {os.path.basename(path)}  size={len(d):,}')
    print(f'   ver={ver} count={count} dataStart=0x{dstart:x} h10=0x{h10:x} h14={h14}')
    assert dstart==0x18+count*16, (dstart, 0x18+count*16)
    ents=[]
    for i in range(count):
        o=0x18+i*16
        eid,off,typ,flg,ln=struct.unpack_from('<IIHHI',d,o)
        ents.append((eid,off,typ,flg,ln))
    # verify id sequential & offsets monotonic
    seq=all(ents[i][0]==ents[0][0]+i for i in range(count))
    mono=all(ents[i][1]<=ents[i+1][1] for i in range(count-1))
    end_ok=all(e[1]+e[4]<=len(d) for e in ents)
    print(f'   id 0x{ents[0][0]:08x} 连续={seq}  偏移单调={mono}  越界={not end_ok}')
    print(f'   type 分布: {sorted(collections.Counter(e[2] for e in ents).items())}')
    print(f'   flags 分布: {sorted(collections.Counter(e[3] for e in ents).items())}')
    print(f'   len 范围: {min(e[4] for e in ents)} .. {max(e[4] for e in ents)}')
    return d,ents
for p in sys.argv[1:]:
    d,ents=parse(p)
    # dump first few payloads of each type
    seen=collections.defaultdict(int)
    print('   --- payload samples ---')
    for eid,off,typ,flg,ln in ents:
        if seen[typ]<2:
            seen[typ]+=1
            print(f'   type={typ:<3} id=0x{eid:08x} off=0x{off:x} len={ln:<6} : {d[off:off+min(48,ln)].hex(" ")}')
    print()
