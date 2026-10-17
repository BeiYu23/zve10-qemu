import struct,subprocess,sys
path=sys.argv[1]
rd=open(path,'rb').read()
out=subprocess.check_output(['readelf','-SW',path],text=True); secs={}
for l in out.splitlines():
    l=l.strip()
    if not l.startswith('['):continue
    p=l.replace('[','').replace(']',' ').split()
    try: secs[p[1]]=(int(p[3],16),int(p[4],16),int(p[5],16))
    except: pass
rod=secs['.rodata']; drr=secs['.data.rel.ro']
# all relocation targets in .data.rel.ro -> these are referenced vaddrs
rl=subprocess.check_output(['readelf','-rW',path],text=True)
refs=set()
for l in rl.splitlines():
    p=l.split()
    if len(p)>=3 and p[2]=='R_ARM_RELATIVE':
        off=int(p[0],16)
        if drr[0]<=off<drr[0]+drr[2]:
            fo=drr[1]+(off-drr[0])
            v=struct.unpack_from('<I',rd,fo)[0]
            refs.add(v)
# collect strings in .rodata
a,o,s=rod
data=rd[o:o+s]
i=0; strs=[]
while i < len(data):
    if data[i]==0: i+=1; continue
    j=data.find(b'\0',i)
    if j<0: break
    strs.append((a+i, data[i:j].decode('latin1')))
    i=j+1
print('referenced vaddrs: %d ; .rodata strings: %d'%(len(refs),len(strs)))
free=[(v,t) for v,t in strs if v not in refs]
print('--- .rodata strings NOT referenced by any relocation (free space candidates), len>=13 ---')
n=0
for v,t in free:
    if len(t)+1>=13:
        n+=1
        if n<=40: print('  vaddr=0x%05x fileoff=0x%05x len=%2d  %r'%(v,o+(v-a),len(t),t))
print('  ... total free candidates len>=13: %d'%n)
print('--- zero runs >=16 bytes inside .rodata/.data.rel.ro ---')
for name in ('.rodata','.data.rel.ro'):
    aa,oo,ss=secs[name]; d=rd[oo:oo+ss]
    run=0;start=0
    runs=[]
    for k in range(len(d)):
        if d[k]==0:
            if run==0: start=k
            run+=1
        else:
            if run>=16: runs.append((aa+start,run))
            run=0
    if run>=16: runs.append((aa+start,run))
    print(' ',name,'runs:',[('0x%x'%v,n) for v,n in runs][:20])
# PT_LOAD slack
print('--- PT_LOAD segments ---')
print(subprocess.check_output(['readelf','-lW',path],text=True).split('Section to Segment mapping')[0])
