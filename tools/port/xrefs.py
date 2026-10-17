import subprocess,sys,struct
path=sys.argv[1]
rd=open(path,'rb').read()
out=subprocess.check_output(['readelf','-SW',path],text=True); secs=[]
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
    try: return rd[o:e].decode('ascii')
    except: return None
t=[s for s in secs if s[0]=='.text'][0]
tname,ta,to,ts=t
d=rd[to:to+ts]
# Thumb: LDR literal = 01001 Rt imm8 (2 bytes). Execute in thumb state sequentially is complex;
# instead scan every 2-byte offset for the pattern and validate the literal resolves to a string.
hits={}
for i in range(0,len(d)-16,2):
    hw=struct.unpack_from('<H',d,i)[0]
    if (hw & 0xF800)!=0x4800: continue
    rt=(hw>>8)&7; imm=hw&0xff
    addr=ta+i
    lit_addr=((addr+4)&~3)+imm*4
    lo=v2o(lit_addr)
    if lo is None: continue
    lit=struct.unpack_from('<I',rd,lo)[0]
    # look for add rt,pc within next 12 bytes
    for k in range(2,14,2):
        if i+k+2>len(d): break
        hw2=struct.unpack_from('<H',d,i+k)[0]
        if hw2==(0x4478|rt):
            add_addr=ta+i+k
            tgt=(add_addr+4)+lit
            s=rdstr(tgt)
            if s and len(s)>2 and all(32<=ord(c)<127 for c in s):
                hits.setdefault(add_addr,(s,addr,lit,tgt))
            break
for a in sorted(hits):
    s,ldr,lit,tgt=hits[a]
    print('add@0x%05x  ldr@0x%05x  lit=0x%08x  tgt=0x%08x  %r'%(a,ldr,lit,tgt,s))
