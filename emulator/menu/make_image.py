#!/usr/bin/env python3
"""Create a menu-only image copy with explicit, verified modifications.
Never writes the input. debugfs diagnostics are checked by content readback.
"""
import argparse, hashlib, os, pathlib, shutil, subprocess

def sha(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def dfs(img, request):
    r=subprocess.run(['debugfs','-w','-R',request,str(img)],capture_output=True,text=True)
    if r.returncode: raise RuntimeError(r.stderr)
    return r.stdout+r.stderr

def main():
    p=argparse.ArgumentParser(); p.add_argument('--base',required=True); p.add_argument('--output',required=True)
    p.add_argument('--work',required=True); p.add_argument('--host',required=True); p.add_argument('--ref',default='/root/fw/ref_e10/root/lib')
    p.add_argument('--module',help='replacement for usr:/kmod/grm_gles.ko')
    a=p.parse_args(); base=pathlib.Path(a.base).resolve(); out=pathlib.Path(a.output).resolve(); w=pathlib.Path(a.work).resolve()
    if out==base or out.exists(): raise SystemExit('output must be new and different from base')
    w.mkdir(parents=True,exist_ok=True)
    parts={}
    for name,off,n in [('root',168<<20,8<<20),('usr',732<<20,300<<20)]:
        path=w/(name+'-menu.ext2')
        if path.exists(): raise SystemExit('work partition already exists: '+str(path))
        with base.open('rb') as f, path.open('xb') as g:
            f.seek(off); remain=n
            while remain:
                b=f.read(min(remain,1<<20))
                if not b: raise RuntimeError('short base')
                g.write(b); remain-=len(b)
        parts[name]=(path,off)
    empty=w/'empty-preload'; empty.write_text('\n')
    changes=[('root','/etc/ld.so.preload',empty),('usr','/bin/im.elf',pathlib.Path(a.host))]
    for name in ['libc-2.11.2.so','libpthread-2.11.2.so']:
        changes.append(('root','/lib/'+name,pathlib.Path(a.ref)/name))
    if a.module:
        changes.append(('usr','/kmod/grm_gles.ko',pathlib.Path(a.module)))
    for part,target,source in changes:
        image=parts[part][0]
        dfs(image,'rm '+target)
        dfs(image,'write '+str(source.resolve())+' '+target)
        mode='0100644' if ('preload' in target or target.endswith('.ko')) else '0100755'
        dfs(image,'set_inode_field '+target+' mode '+mode)
        checked=w/('check-'+part+'-'+target.rsplit('/',1)[1])
        dfs(image,'dump '+target+' '+str(checked))
        if not checked.exists() or sha(checked)!=sha(source): raise RuntimeError('readback failed: '+target)
        print('VERIFIED',part,target,sha(source),flush=True)
    for path,off in parts.values():
        r=subprocess.run(['e2fsck','-fn',str(path)],capture_output=True,text=True)
        (w/(path.stem+'-fsck.txt')).write_text(r.stdout+r.stderr)
        if r.returncode: raise RuntimeError('fsck failed '+str(path))
    shutil.copyfile(base,out)
    with out.open('r+b') as f:
        for path,off in parts.values():
            f.seek(off)
            with path.open('rb') as g: shutil.copyfileobj(g,f)
        f.flush(); os.fsync(f.fileno())
    print('CREATED',out,flush=True)
if __name__=='__main__': main()
