import struct, sys, subprocess

def parse(path):
    rd = open(path,'rb').read()
    # parse section headers to map vaddr->fileoff
    out = subprocess.check_output(['readelf','-SW',path],text=True)
    secs={}
    for line in out.splitlines():
        line=line.strip()
        if not line.startswith('['): continue
        parts=line.replace('[','').replace(']',' ').split()
        # [Nr] Name Type Addr Off Size ...
        try:
            nr=int(parts[0]); name=parts[1]; addr=int(parts[3],16); off=int(parts[4],16); size=int(parts[5],16)
        except Exception: continue
        secs[name]=(addr,off,size)
    drr=secs['.data.rel.ro']; rod=secs['.rodata']
    base=drr[0]
    # find imdb_raw: dynsym
    ds=subprocess.check_output(['readelf','-sW',path],text=True)
    imdb=None
    for l in ds.splitlines():
        if 'imdb_raw' in l:
            p=l.split(); imdb=int(p[1],16)
    def v2o(v):
        for n,(a,o,s) in secs.items():
            if a<=v<a+s and s>0: return o+(v-a)
        return None
    def rdstr(v):
        o=v2o(v)
        if o is None: return '<bad 0x%x>'%v
        e=rd.find(b'\0',o)
        return rd[o:e].decode('latin1')
    recbase=imdb+0x84
    recs=[]
    i=0
    while True:
        r=recbase+44*i
        o=v2o(r)
        if o is None: break
        f0,f1,f2,f3=struct.unpack_from('<IIII',rd,o)
        if f0==40: 
            recs.append(('TERMINATOR',i,r,f0,f1,f2,f3))
            break
        ptrs=struct.unpack_from('<IIIIIII',rd,o+0x10)
        recs.append((i, r, f0,f1,f2,f3, [rdstr(p) for p in ptrs]))
        i+=1
    return secs, imdb, recbase, recs, rd, v2o

for path in sys.argv[1:]:
    print('='*100)
    print(path)
    secs, imdb, recbase, recs, rd, v2o = parse(path)
    print('imdb_raw vaddr=0x%x  record base=0x%x  nrecords=%d'%(imdb,recbase,len(recs)))
    print('first rec header:', recs[0][:6])
    print('last:', recs[-1][:6])
    print('--- records whose 1st pointer looks like a .so or matches appFw/viewUnified ---')
    for r in recs:
        if r[0]=='TERMINATOR': print('TERM', r); continue
        idx,addr,f0,f1,f2,f3,ptrs = r
        joined=' | '.join(ptrs)
        if 'appFw' in joined or 'viewUnified' in joined or 'gui.so' in joined:
            print('idx=%d addr=0x%x hdr=[%d,%d,%d,0x%x] ptrs=%s'%(idx,addr,f0,f1,f2,f3,ptrs))
