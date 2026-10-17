import struct,subprocess,sys
def secs_of(path):
    out=subprocess.check_output(['readelf','-SW',path],text=True); s={}
    for l in out.splitlines():
        l=l.strip()
        if not l.startswith('['):continue
        p=l.replace('[','').replace(']',' ').split()
        try: s[p[1]]=(int(p[3],16),int(p[4],16),int(p[5],16))
        except: pass
    return s
def go(path,lo,hi):
    rd=open(path,'rb').read(); secs=secs_of(path)
    ds=subprocess.check_output(['readelf','-sW',path],text=True)
    imdb=[int(l.split()[1],16) for l in ds.splitlines() if 'imdb_raw' in l][0]
    def v2o(v):
        for n,(a,o,s) in secs.items():
            if a<=v<a+s and s>0: return o+(v-a)
    def st(v):
        o=v2o(v)
        if o is None: return '<0x%x>'%v
        e=rd.find(b'\0',o); return rd[o:e].decode('latin1')
    print('### ',path)
    for i in range(lo,hi):
        r=imdb+0x84+44*i; o=v2o(r)
        f0,f1,f2,f3=struct.unpack_from('<IIII',rd,o)
        if f0==40: print(i,'TERMINATOR'); break
        ptrs=struct.unpack_from('<IIIIIII',rd,o+0x10)
        print('idx=%3d vaddr=0x%05x fileoff=0x%05x hdr=0x%08x,0x%08x,0x%08x,0x%08x id=0x%04x  slots(+0x10..+0x28)=%s'%(
            i,r,o,f0,f1,f2,f3,f3&0xffff,[st(p) for p in ptrs]))
go(sys.argv[1],int(sys.argv[2]),int(sys.argv[3]))
