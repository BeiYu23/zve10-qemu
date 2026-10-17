import subprocess,sys,struct
path=sys.argv[1]
rd=open(path,'rb').read()
out=subprocess.check_output(['readelf','-SW',path],text=True)
etyp=subprocess.check_output(['readelf','-hW',path],text=True)
secs=[]
for l in out.splitlines():
    l=l.strip()
    if not l.startswith('['):continue
    p=l.replace('[','').replace(']',' ').split()
    try: secs.append((p[1],int(p[3],16),int(p[4],16),int(p[5],16)))
    except: pass
def v2o(v):
    for n,a,o,s in secs:
        if a<=v<a+s and s>0: return o+(v-a)
def rdstr(v):
    o=v2o(v)
    if o is None: return None
    e=rd.find(b'\0',o)
    if e<0 or e-o>120: return None
    try:
        s=rd[o:e].decode('ascii')
        if len(s)>2 and all(32<=ord(c)<127 for c in s): return s
    except: pass
tn,ta,to,ts=[s for s in secs if s[0]=='.text'][0]
d=rd[to:to+ts]
hits={}
for i in range(0,len(d)-14,2):
    hw=struct.unpack_from('<H',d,i)[0]
    if (hw&0xF800)!=0x4800: continue
    rt=(hw>>8)&7; imm=hw&0xff
    addr=ta+i
    pa=((addr+4)&~3)+imm*4
    po=v2o(pa)
    if po is None: continue
    lit=struct.unpack_from('<I',rd,po)[0]
    # case A: absolute literal
    s=rdstr(lit)
    if s: hits.setdefault(addr,(s,'abs'))
    # case B: add Rt,pc within next 12 bytes
    for k in range(2,14,2):
        if i+k+2>len(d): break
        if struct.unpack_from('<H',d,i+k)[0]==(0x4478|rt):
            s2=rdstr((ta+i+k+4)+lit)
            if s2: hits.setdefault(addr,(s2,'pc+lit'))
            break
for a in sorted(hits):
    s,how=hits[a]
    print('0x%05x  %-7s %r'%(a,how,s))
