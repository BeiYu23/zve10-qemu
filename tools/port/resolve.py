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
    if e<0 or e-o>200: return None
    try: return rd[o:e].decode('ascii')
    except: return None
# args: literal_vaddr add_instr_vaddr ...
for i in range(2,len(sys.argv),2):
    lv=int(sys.argv[i],16); av=int(sys.argv[i+1],16)
    lo=v2o(lv); lit=struct.unpack_from('<I',rd,lo)[0]
    pc=(av+4)
    print('literal@0x%x = 0x%08x ; add@0x%x pc=0x%x -> sum=0x%x'%(lv,lit,av,pc,(pc+lit)&0xffffffff))
    for cand,name in ((lit,'literal-as-vaddr'),(pc+lit,'pc+literal')):
        s=rdstr(cand)
        if s: print('    %-18s 0x%08x -> %r'%(name,cand,s))
