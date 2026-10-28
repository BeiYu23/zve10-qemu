/* ZV-E10 guest-side injection helper (v6, clean rewrite).
 * Preloaded only in the camera app process. Console = /dev/ttyAM1.
 * Commands: #S state  #P scan  #L nodes  #M osal  #B <views>  #V <n>
 *           #D <addr> <len>  #W words  #Q <addr> <hex>  #C <id>  #G <a> <b> */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <dirent.h>
#include <sys/syscall.h>

static void *g_handle, *g_inst;
static unsigned long g_base, g_end, g_views;
static uint32_t g_cand[32]; static int g_ncand;
static int g_ready, g_tty_logged;

static void klog(const char *s){ size_t n=strlen(s); write(1,s,n);
    int fd=open("/dev/kmsg",1); if(fd>=0){write(fd,s,n);close(fd);} }
static void klogf(const char *fmt,...){ char b[192]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(b,sizeof b,fmt,ap); va_end(ap); if(n>0) klog(b); }

static int is_app(void){
    int fd=open("/proc/self/cmdline",0); if(fd<0) return 0;
    char b[256]; int n=read(fd,b,sizeof b-1); close(fd); if(n<=0) return 0;
    b[n]=0; return strstr(b,"bootin")!=0; }

/* ---- mapped ranges + safe reads ---- */
struct rng { unsigned long s, e; };
static struct rng g_rng[512]; static int g_nrng;
static void load_ranges(void){
    g_nrng=0; int fd=open("/proc/self/maps",0); if(fd<0) return;
    static char buf[1<<20]; ssize_t n,used=0;
    while((n=read(fd,buf+used,sizeof buf-used-1))>0) used+=n;
    buf[used]=0; close(fd);
    for(char *l=buf; l&&*l && g_nrng<512; ){
        char *nl=strchr(l,'\n'); if(nl)*nl=0;
        unsigned long a=0,b=0; char perm[8]={0};
        if(sscanf(l,"%lx-%lx %7s",&a,&b,perm)==3 && perm[0]=='r'){
            g_rng[g_nrng].s=a; g_rng[g_nrng].e=b; g_nrng++; }
        l = nl? nl+1 : 0; } }
static int safe_u32(unsigned long a, unsigned *out){
    for(int i=0;i<g_nrng;i++) if(a>=g_rng[i].s && a+4<=g_rng[i].e){ *out=*(unsigned*)a; return 1; }
    return 0; }

/* ---- discovery of libObj ---- */
static int discover(void){
    if(g_base) return 1;
    void *h=dlopen("/usr/lib/libObj.so",RTLD_LAZY); if(!h) return 0;
    g_handle=h;
    void *getter=dlsym(h,"_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    Dl_info i;
    if(getter && dladdr(getter,&i) && i.dli_fbase){
        g_base=(unsigned long)i.dli_fbase; g_end=g_base+0x1498e7c;
        klogf("KEYINJECT base=%08x\n",(unsigned)g_base); return 1; }
    return 0; }

static void scan(void){
    g_ncand=0; if(!g_inst) return;
    uint32_t *p=(uint32_t*)g_inst;
    for(int i=0;i<0x200/4 && g_ncand<32;i++){
        uint32_t v=p[i];
        int in_obj=(v>=g_base&&v<g_end), in_low=(v>0x1000&&v<0x20000000);
        if((v&1)&&(in_obj||in_low)){ g_cand[g_ncand]=v;
            klogf("KEYINJECT cand[%d] off=%03x val=%08x\n",g_ncand,i*4,v); g_ncand++; } }
    klogf("KEYINJECT scan inst=%08x cands=%d\n",(unsigned)(unsigned long)g_inst,g_ncand); }

static void try_init(void){
    void *(*gi)(void)=(void*(*)(void))(g_base+0x8c22a0+1);
    void *inst=gi();
    if(inst){ g_inst=inst; scan(); g_ready=1; klogf("KEYINJECT ready inst=%08x\n",(unsigned)inst); } }

/* ---- manager / view driving ---- */
static unsigned scan_manager(void){
    if(!g_nrng) load_ranges();
    klogf("KEYINJECT ranges=%d\n",g_nrng);
    for(unsigned off=0x1492000; off<0x1492600; off+=4){
        unsigned v=0,magic=0;
        if(!safe_u32(g_base+off,&v)) continue;
        if(v<0x10000||v>0x10000000) continue;
        if(!safe_u32(v,&magic)||magic!=43) continue;
        klogf("KEYINJECT mgr slot+%05x=%08x\n",off,v);
        unsigned slots=0;
        if(!safe_u32(v+32,&slots)||slots<0x10000||slots>0x10000000) return 0;
        klogf("KEYINJECT slots=%08x\n",slots);
        for(unsigned i=0;i<43;i++){
            unsigned e1=0,e2=0;
            if(!safe_u32(slots+i*16+4,&e1)) break;
            if(!safe_u32(slots+i*16+8,&e2)) break;
            if(!e1) continue;
            if(e2==0xa3){ klogf("KEYINJECT root=%08x\n",e1); return e1; } }
        return 0; }
    klog("KEYINJECT no mgr\n"); return 0; }

/* --- run a view request on the UI thread via a signal --- */
static volatile unsigned g_req_n = 0xffffffffu;
static volatile unsigned g_req_root;
static void ui_sig(int sig)
{
    klogf("KEYINJECT SIG %d hit tid=%ld\n", sig, syscall(224));
    unsigned n = g_req_n; if (n == 0xffffffffu) return;
    g_req_n = 0xffffffffu;
    if (!g_views || !g_req_root) return;
    klogf("KEYINJECT U exec n=%u root=%08x\n", n, g_req_root);
    ((void(*)(int))(g_views + 0x590f35 + 1))(1);
    ((void(*)(void*,int))(g_views + 0x47de27 + 1))((void *)(g_req_root + 0x154), (int)n);
    klogf("KEYINJECT U done n=%u\n", n);
}
static void install_sig(void)
{
    static int done;
    if (done) return;
    done = 1;
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = ui_sig; sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, 0);
}
static void *vcall(void *a){
    int n=(int)(unsigned long)a;
    if(!g_views||!g_base){ klog("KEYINJECT V no args\n"); return 0; }
    unsigned root=scan_manager();
    if(!root) return 0;
    ((void(*)(int))(g_views+0x590f35+1))(1);
    klogf("KEYINJECT V mode n=%d\n",n);
    ((void(*)(void*,int))(g_views+0x47de27+1))((void*)(root+0x154),n);
    klogf("KEYINJECT V done n=%d\n",n);
    return 0; }

struct earg { unsigned long fn, a1, a2; };
static void *ecall(void *p){
    struct earg *e=(struct earg*)p;
    klogf("KEYINJECT E call fn=%08x a1=%08x a2=%08x\n",(unsigned)e->fn,(unsigned)e->a1,(unsigned)e->a2);
    ((void(*)(unsigned long,unsigned long))(e->fn|1))(e->a1,e->a2);
    klogf("KEYINJECT E done\n");
    return 0; }

static void *qcall(void *a){ (void)a; return 0; }

/* --- UI-thread hook on drawFramebuffer (same technique as the reference lib) --- */
static int (*orig_draw)(int,int,int,int);
static volatile unsigned g_pending = 0xffffffffu;
static volatile unsigned g_page_root;

static void thumb_jump(unsigned long at, unsigned long target)
{
    unsigned char c[8] = {0xdf,0xf8,0x00,0xf0,0,0,0,0};
    unsigned t = (unsigned)(target | 1); memcpy(c+4,&t,4);
    if (mprotect((void *)(at & ~4095UL), 4096, PROT_READ|PROT_WRITE|PROT_EXEC)) return;
    memcpy((void *)at, c, 8);
    __builtin___clear_cache((char *)at, (char *)at + 8);
}

static int hooked_draw(int a,int b,int c,int d)
{
    int r = orig_draw ? orig_draw(a,b,c,d) : 0;
    unsigned p = g_pending;
    if (p != 0xffffffffu && g_views) {
        g_pending = 0xffffffffu;
        klogf("KEYINJECT HDRAW exec page=%u\n", p);
        ((void(*)(int))(g_views + 0x590f35 + 1))(1);
        if (g_page_root) ((void(*)(void*,int))(g_views + 0x47de27 + 1))((void *)(g_page_root + 0x154), (int)p);
        klogf("KEYINJECT HDRAW done page=%u\n", p);
    }
    return r;
}

static int menu_is_drawn(void)
{
    static int fd = -1;
    if (fd < 0) fd = open("/dev/mem", 0);
    if (fd < 0) return 0;
    static unsigned char buf[640*480*2];
    if (lseek(fd, 0x22c48000, 0) != 0x22c48000) return 0;
    if (read(fd, buf, sizeof buf) != (long)sizeof buf) return 0;
    unsigned seen[8]; int n = 0;
    for (unsigned i = 0; i + 1 < sizeof buf && n < 8; i += 2) {
        unsigned v = buf[i] | (buf[i+1] << 8);
        int dup = 0; for (int k = 0; k < n; k++) if (seen[k] == v) dup = 1;
        if (!dup) seen[n++] = v;
    }
    return n >= 4;
}

/* relocating trampoline hook (ported from the reference install_rm_hook) */
static int hk_install(unsigned long address, const unsigned char want[8],
                      void *replacement, void **original)
{
    if (memcmp((void *)address, want, 8)) { klog("KEYINJECT hk prologue mismatch\n"); return -1; }
    unsigned short load = *(unsigned short *)(address + 6);
    if ((load & 0xf800) != 0x4800) { klogf("KEYINJECT hk no ldr (0x%04x)\n", load); return -1; }
    unsigned long lit_addr = ((address + 10) & ~3UL) + (load & 255) * 4;
    unsigned literal = 0;
    if (!safe_u32(lit_addr, &literal)) { klog("KEYINJECT hk literal unreadable\n"); return -1; }
    unsigned char *code = mmap(0, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { klog("KEYINJECT hk mmap fail\n"); return -1; }
    if (mprotect((void *)(address & ~4095UL), 4096, PROT_READ|PROT_WRITE|PROT_EXEC)) {
        klog("KEYINJECT hk mprotect fail\n"); return -1; }
    memcpy(code, (void *)address, 12);
    unsigned short relocated = (load & 0xff00) | 3;      /* pool at +20, PC at +8 */
    memcpy(code + 6, &relocated, 2);
    thumb_jump((unsigned long)code + 12, (address + 12) | 1);
    memcpy(code + 20, &literal, 4);
    __builtin___clear_cache((char *)code, (char *)code + 24);
    *original = (void *)((unsigned long)code | 1);
    thumb_jump(address, (unsigned long)replacement);
    return 0;
}

static int (*orig_frame)(int,int,int,int);
static unsigned g_frame_calls;
static int hooked_frame(int a, int b, int c, int d)
{
    int r = orig_frame ? orig_frame(a,b,c,d) : 0;
    g_frame_calls++;
    if (g_frame_calls <= 3 || (g_frame_calls % 200) == 0)
        klogf("KEYINJECT FRAME tick n=%u\n", g_frame_calls);
    unsigned p = g_pending;
    if (p != 0xffffffffu && g_views) {
        g_pending = 0xffffffffu;
        klogf("KEYINJECT FRAME exec page=%u\n", p);
        ((void(*)(int))(g_views + 0x590f35 + 1))(1);
        if (g_page_root) ((void(*)(void*,int))(g_views + 0x47de27 + 1))((void *)(g_page_root + 0x154), (int)p);
        klogf("KEYINJECT FRAME done page=%u\n", p);
    }
    return r;
}

static void do_hook(void)
{
    static int done;
    if (done) return;
    if (!g_base) { klog("KEYINJECT hk no base\n"); return; }
    if (!g_nrng) load_ranges();
    static const unsigned char want[8] = {0x2d,0xe9,0xf0,0x47,0x82,0xb0,0x9a,0x4c};
    void *orig = 0;
    if (hk_install(g_base + 0x7748e0, want, (void *)((unsigned long)hooked_frame | 1), &orig) == 0) {
        orig_frame = (int(*)(int,int,int,int))orig;
        done = 1;
        klogf("KEYINJECT hk hooked frame at %08x\n", (unsigned)(g_base + 0x7748e0));
    } else done = 1;
}

/* early registration attempt: run once as soon as uipc is present */
static void try_register_early(void)
{
    static int done;
    if (done) return;
    void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!h) return;
    done = 1;
    int (*reg)(unsigned, void*) = dlsym(h, "osal_reg_sync_msg_queue_cb");
    klogf("KEYINJECT early reg api=%p\n", (void*)reg);
    if (!reg) return;
    int ok = 0, first_err = 0;
    for (unsigned q = 0x3e0100; q <= 0x3e01ff; q++) {
        int r = reg(q, (void*)0);
        if (r == 0) ok++; else if (!first_err) first_err = r;
    }
    klogf("KEYINJECT early reg 3e01xx ok=%d first_err=%d\n", ok, first_err);
    ok = 0;
    for (unsigned q = 0x00410000; q <= 0x004100ff; q++) {
        int r = reg(q, (void*)0);
        if (r == 0) ok++;
    }
    klogf("KEYINJECT early reg 0041xx ok=%d\n", ok);
}

static unsigned long g_qid;
static volatile unsigned g_rq_q[32], g_rq_n[32], g_rq_c;
static pid_t g_ui_tid;
static void *sampler(void *a);
static void discover_qid(void);
static void inject_key(unsigned,unsigned,unsigned,unsigned,unsigned);

static void *thread(void *a){
    (void)a;
    for(;;){
        try_register_early();
        if(!g_ready){ if(discover()) try_init(); usleep(200000); continue; }
        static int fd=-1;
        if(fd<0){ fd=open("/dev/ttyAM1",O_RDONLY|O_NONBLOCK);
                  if(fd<0){ usleep(300000); continue; }
                  if(!g_tty_logged){ g_tty_logged=1; klogf("KEYINJECT tty ok fd=%d\n",fd); } }
        char buf[256]; int n;
        while((n=read(fd,buf,sizeof buf-1))>0){
            buf[n]=0;
            for(char *t=strtok(buf,"\n"); t; t=strtok(0,"\n")){
                char *m;
                m=strstr(t,"#S"); if(m){ klogf("KEYINJECT state inst=%08x base=%08x views=%08x\n",
                      (unsigned)(unsigned long)g_inst,(unsigned)g_base,(unsigned)g_views); continue; }
                m=strstr(t,"#Rq"); if(m){
                      klogf("KEYINJECT Rq count=%u\n", g_rq_c);
                      for (unsigned i = 0; i < g_rq_c && i < 32; i++)
                          klogf("KEYINJECT RQ qid=%08x n=%u\n", g_rq_q[i], g_rq_n[i]);
                      continue; }
                m=strstr(t,"#Wa"); if(m){
                      /* Walk all threads: attach briefly, read PC; if PC is inside
                       * libosal*, log r0 (the queue id being waited on). */
                      static unsigned long osal_b, osal_e;
                      if (!osal_b) {
                        unsigned long b = 0;
                        FILE *f = fopen("/proc/self/maps","r");
                        if (f) { char line[256];
                          while (fgets(line,sizeof line,f)) {
                            if (strstr(line,"libosal") && strstr(line,"r-xp")) {
                              sscanf(line,"%lx-%lx",&b,&osal_e); osal_b=b; break; } }
                          fclose(f); }
                        klogf("KEYINJECT Wa osal=%08x-%08x\n",(unsigned)osal_b,(unsigned)osal_e);
                      }
                      DIR *d = opendir("/proc/self/task");
                      int hit=0;
                      if (d) { struct dirent *e2;
                        while ((e2 = readdir(d)) && hit < 12) {
                          if (e2->d_name[0]=='.') continue;
                          pid_t tid = (pid_t)atoi(e2->d_name);
                          if (tid == (pid_t)syscall(224)) continue;
                          if (ptrace(PTRACE_ATTACH, tid, 0, 0)) continue;
                          int st; waitpid(tid,&st,0);
                          struct user_regs r;
                          if (!ptrace(PTRACE_GETREGS, tid, 0, &r)) {
                            unsigned pc=(unsigned)r.uregs[15];
                            if (pc >= osal_b && pc < osal_e) {
                              klogf("KEYINJECT Wa tid=%d pc=%08x r0=%08x r1=%08x r2=%08x\n",
                                    (int)tid, pc, (unsigned)r.uregs[0], (unsigned)r.uregs[1], (unsigned)r.uregs[2]);
                              hit++; } }
                          ptrace(PTRACE_DETACH, tid, 0, 0); } 
                        closedir(d); }
                      klogf("KEYINJECT Wa hits=%d\n", hit);
                      continue; }
                m=strstr(t,"#Mh"); if(m){ char *p=m+3;
                      unsigned q=(unsigned)strtoul(p,&p,0);
                      void *h=dlopen("/usr/lib/libosal_uipc.so",RTLD_LAZY);
                      int (*va)(unsigned,void**,unsigned,int)= h?dlsym(h,"osal_valloc_msg_wait"):0;
                      klogf("KEYINJECT Mh qid=%08x api=%p\n", q, (void*)va);
                      if(va){ void *msg=0; int r=va(q,&msg,20,1);
                        klogf("KEYINJECT Mh r=%d msg=%p\n", r, msg);
                        if(!r&&msg){ unsigned *w=(unsigned*)msg;
                          for(int i=-8;i<12;i++)
                            klogf("KEYINJECT MH%+03d=%08x\n", i*4, w[i]); } }
                      continue; }
                m=strstr(t,"#P"); if(m){ scan(); continue; }
                m=strstr(t,"#M"); if(m){ klogf("KEYINJECT osal base=%08x\n",(unsigned)g_base); continue; }
                m=strstr(t,"#B"); if(m){ g_views=strtoul(m+2,0,16);
                      klogf("KEYINJECT views=%08x\n",(unsigned)g_views); continue; }
                m=strstr(t,"#V"); if(m){ int k=0; sscanf(m+2,"%d",&k);
                      g_page_root = g_req_root; g_pending = (unsigned)k;
                      klogf("KEYINJECT V queued page=%d\n", k); continue; }
                m=strstr(t,"#Vold"); if(m){ int k=0; sscanf(m+2,"%d",&k);
                      static unsigned long arg; arg=(unsigned long)k;
                      pthread_t th; pthread_create(&th,0,vcall,(void*)arg);
                      klogf("KEYINJECT V spawn n=%d\n",k); continue; }
                m=strstr(t,"#E"); if(m){ char *p=m+2;
                      static struct earg e; e.fn=strtoul(p,&p,16);
                      e.a1=strtoul(p,&p,16); e.a2=strtoul(p,&p,16);
                      static struct earg e2; e2=e;
                      pthread_t th; pthread_create(&th,0,ecall,&e2);
                      klogf("KEYINJECT E spawn\n"); continue; }
                m=strstr(t,"#U"); if(m){ char *p=m+2;
                      unsigned long vw=strtoul(p,&p,16);
                      unsigned long rt=strtoul(p,&p,16);
                      int nn=(int)strtol(p,&p,10);
                      long tid=strtol(p,&p,10);
                      int sg=(int)strtol(p,&p,10); if(sg<=0) sg=SIGUSR1;
                      install_sig(); signal(SIGURG, ui_sig);
                      g_views=vw; g_req_root=(unsigned)rt; g_req_n=(unsigned)nn;
                      long r = syscall(268, getpid(), (int)tid, sg);
                      if (r) r = syscall(238, (int)tid, sg);
                      klogf("KEYINJECT U send tid=%ld n=%d sig=%d ret=%ld\n", tid, nn, sg, r);
                      continue; }
                m=strstr(t,"#Z"); if(m){ install_sig(); signal(SIGURG, ui_sig);
                      long me=syscall(224);
                      g_req_root=0; g_req_n=0xffffffffu;
                      long r=syscall(268, getpid(), (int)me, SIGUSR1);
                      klogf("KEYINJECT Z selftest tid=%ld ret=%ld\n", me, r);
                      continue; }
                m=strstr(t,"#N"); if(m){ discover_qid(); continue; }
                m=strstr(t,"#Lg"); if(m){ int mode=(int)strtol(m+3,0,0);
                      void *h=dlopen("/usr/lib/libObj.so",RTLD_LAZY);
                      int (*setmode)(int)= h?dlsym(h,"_ZN13LkmLanguageIF7SetModeEi"):0;
                      int (*getmode)(void)= h?dlsym(h,"_ZN13LkmLanguageIF7GetModeEv"):0;
                      klogf("KEYINJECT Lg api=%p get=%p\n",(void*)setmode,(void*)getmode);
                      if(getmode) klogf("KEYINJECT Lg before=%d\n", getmode());
                      if(setmode){ int r=setmode(mode); klogf("KEYINJECT Lg set(%d)=%d\n", mode, r); }
                      if(getmode) klogf("KEYINJECT Lg after=%d\n", getmode());
                      continue; }
                m=strstr(t,"#Kq"); if(m){ char *p=m+3;
                      unsigned q=(unsigned)strtoul(p,&p,0);
                      unsigned k2=(unsigned)strtoul(p,&p,0);
                      int st = 2;
                      if (*p) st = (int)strtol(p,&p,0);
                      g_qid=q;
                      klogf("KEYINJECT Kq qid=%08x key=%u st=%d\n", q, k2, st);
                      if (st == 2) { inject_key(k2,0,1,0,1); inject_key(k2,0,0,0,1); }
                      else inject_key(k2,0,(unsigned)st,0,1);
                      continue; }
                m=strstr(t,"#K"); if(m){ char *p=m+2; unsigned a[5]={0,0,1,0,1};
                      for(int i=0;i<5;i++){ while(*p==' ')p++; if(!*p)break; a[i]=(unsigned)strtoul(p,&p,0); }
                      inject_key(a[0],a[1],a[2],a[3],a[4]); continue; }
                m=strstr(t,"#I"); if(m){ g_ui_tid=(pid_t)strtol(m+2,0,10);
                      klogf("KEYINJECT ui tid=%ld\n",(long)g_ui_tid); continue; }
                m=strstr(t,"#Y"); if(m){ pthread_t th; pthread_create(&th,0,sampler,0);
                      klog("KEYINJECT sampler spawned\n"); continue; }
                m=strstr(t,"#H"); if(m){ do_hook(); continue; }
                m=strstr(t,"#T"); if(m){ g_req_root=strtoul(m+2,0,16);
                      g_page_root=g_req_root;
                      klogf("KEYINJECT page root=%08x\n",(unsigned)g_req_root); continue; }
                m=strstr(t,"#W"); if(m){ unsigned *q=(unsigned*)g_inst;
                      for(int i=0;i<16;i++) klogf("KEYINJECT w+%02x=%08x\n",i*4,q[i]); continue; }
                m=strstr(t,"#Lq"); if(m){ unsigned node=0;
                      { unsigned raw=0; safe_u32((unsigned long)g_inst+0x9c,&raw);
                        klogf("KEYINJECT Lq head=%08x inst=%08x\n", raw, (unsigned)(unsigned long)g_inst); }
                      if(safe_u32((unsigned long)g_inst+0x9c,&node) && node){
                        for(int k=0;k<8&&node;k++){
                          for(int w=0;w<10;w++){ unsigned v=0;
                            if(safe_u32(node+w*4,&v)) klogf("KEYINJECT N%d+%02x=%08x\n",k,w*4,v); }
                          unsigned nx=0; if(!safe_u32(node,&nx)) break; node=nx; } }
                      continue; }
                m=strstr(t,"#L"); if(m){ unsigned node=0;
                      if(safe_u32((unsigned long)g_inst+0x9c,&node)){
                          for(int k=0;k<4&&node;k++){ unsigned nx=0,id=0;
                              safe_u32(node,&nx); safe_u32(node+0x14,&id);
                              klogf("KEYINJECT node%d id=%u\n",k,id); node=nx; } }
                      continue; }
                m=strstr(t,"#D"); if(m){ char *p=m+2; while(*p==' ')p++;
                      unsigned long a=strtoul(p,&p,16); int len=(int)strtol(p,0,0);
                      if(len<=0||len>512) len=64;
                      klogf("KEYINJECT D %08x len=%d\n",(unsigned)a,len);
                      for(int i=0;i<len/4;i++){ unsigned v=0;
                          if(!safe_u32(a+i*4,&v)){ klogf("KEYINJECT d+%03x=??\n",i*4); break; }
                          klogf("KEYINJECT d+%03x=%08x\n",i*4,v); }
                      continue; }
            }
        }
        usleep(100000);
    }
    return 0; }

/* --- silent queue counter (no serial I/O in the hot path) --- */
static volatile unsigned g_rq_q[32], g_rq_n[32], g_rq_c;
static void *g_native_rcv;
void keyinject_on_rcv(unsigned *regs)
{
    if (!g_native_rcv) {
        void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY);   /* already loaded, local */
        if (h) g_native_rcv = dlsym(h, "osal_rcv_msg_tmo");
        if (!g_native_rcv) {
            g_native_rcv = dlvsym(RTLD_NEXT, "osal_rcv_msg_tmo", "GLIBC_2.4");
            if (!g_native_rcv) g_native_rcv = dlsym(RTLD_NEXT, "osal_rcv_msg_tmo");
        }
    }
    unsigned q = regs[0];
    for (unsigned i = 0; i < g_rq_c && i < 32; i++)
        if (g_rq_q[i] == q) { g_rq_n[i]++; return; }
    if (g_rq_c < 32) { g_rq_q[g_rq_c] = q; g_rq_n[g_rq_c] = 1; g_rq_c++; }
}

__attribute__((naked)) int osal_rcv_msg_tmo(void)
{
    __asm__ volatile(
        "push {r0, r1, r2, r3, r4, lr}\n"
        "mov  r0, sp\n"
        "bl   keyinject_on_rcv\n"
        "pop  {r0, r1, r2, r3, r4, lr}\n"
        "ldr  ip, =g_native_rcv\n"
        "ldr  ip, [ip]\n"
        "cmp  ip, #0\n"
        "bxne ip\n"
        "mvn  r0, #0\n"
        "bx   lr\n");
}

/* --- InfraInput queue discovery + key message injection (a7c2-re method) --- */
static void discover_qid(void)
{
    if (!g_base || !g_inst) { klog("KEYINJECT qid no inst\n"); return; }
    void *(*helper)(void *) = (void *(*)(void *))(g_base + 0x8c2690 + 1);
    for (int i = 0; i < 20; i++) {
        void *o = helper(g_inst);
        unsigned v20 = 0, v1c = 0;
        if (o) { safe_u32((unsigned long)o + 0x20, &v20); safe_u32((unsigned long)o + 0x1c, &v1c); }
        klogf("KEYINJECT qpoll i=%d obj=%08x +20=%08x +1c=%08x\n", i,
              (unsigned)(unsigned long)o, v20, v1c);
        if (v20 && v20 != 0xffffffffu) { g_qid = v20; break; }
        usleep(500000);
    }
    if (g_qid) klogf("KEYINJECT qid selected=%08x\n", (unsigned)g_qid);
    void *obj = helper(g_inst);
    klogf("KEYINJECT helper=%08x\n", (unsigned)(unsigned long)obj);
    if (obj) for (unsigned o2 = 0; o2 < 0x100; o2 += 4) {
        unsigned v = 0;
        if (safe_u32((unsigned long)obj + o2, &v)) klogf("KEYINJECT H+%02x=%08x\n", o2, v);
    }
    if (!obj) return;
    unsigned v1 = 0, v2 = 0;
    for (unsigned off = 0; off < 0x40; off += 4) {
        unsigned v = 0;
        if (safe_u32((unsigned long)obj + off, &v))
            klogf("KEYINJECT h+%02x=%08x\n", off, v);
    }
    if (safe_u32((unsigned long)obj + 0x20, &v1)) g_qid = v1;
    (void)v2;
}

static void inject_key(unsigned key, unsigned sub, unsigned status, unsigned ex, unsigned from)
{
    if (!g_qid) { klog("KEYINJECT no qid\n"); return; }
    void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY);
    if (!h) { klog("KEYINJECT no uipc\n"); return; }
    int (*valloc)(unsigned, void **, unsigned, int) = dlsym(h, "osal_valloc_msg_wait");
    int (*setsrc)(void *, unsigned) = dlsym(h, "osal_set_msg_src");
    int (*sndmsg)(unsigned, void *) = dlsym(h, "osal_snd_msg");
    klogf("KEYINJECT api valloc=%p setsrc=%p snd=%p\n", (void*)valloc, (void*)setsrc, (void*)sndmsg);
    if (!valloc || !sndmsg) return;
    void *msg = 0;
    int r = valloc(g_qid, &msg, 20, 1);
    klogf("KEYINJECT valloc r=%d msg=%08x\n", r, (unsigned)(unsigned long)msg);
    if (r || !msg) return;
    unsigned *w = (unsigned *)msg;
    w[0] = key; w[1] = sub; w[2] = status; w[3] = ex; w[4] = from;
    if (setsrc) setsrc(msg, 0x000a0001u);
    r = sndmsg(g_qid, msg);
    klogf("KEYINJECT snd qid=%08x key=%u st=%u r=%d\n", (unsigned)g_qid, key, status, r);
}

/* --- ptrace sampling of the UI thread (forked helper process) --- */
static void *sampler(void *a)
{
    (void)a;
    pid_t p = fork();
    if (p != 0) return 0;                       /* parent keeps running */
    /* child: sample the UI thread PC */
    if (!g_ui_tid) { klog("KEYINJECT smp no tid\n"); _exit(0); }
    if (ptrace(PTRACE_ATTACH, g_ui_tid, 0, 0)) { klogf("KEYINJECT smp attach fail\n"); _exit(0); }
    int st; waitpid(g_ui_tid, &st, 0);
    for (int i = 0; i < 300; i++) {
        struct user_regs r;
        if (ptrace(PTRACE_GETREGS, g_ui_tid, 0, &r)) break;
        unsigned pc = (unsigned)r.uregs[15];
        if (pc >= g_base && pc < g_end) klogf("KEYINJECT SAMPLE %05x\n", pc - (unsigned)g_base);
        if (ptrace(PTRACE_DETACH, g_ui_tid, 0, 0)) break;
        usleep(20000);
        if (ptrace(PTRACE_ATTACH, g_ui_tid, 0, 0)) break;
        waitpid(g_ui_tid, &st, 0);
    }
    ptrace(PTRACE_DETACH, g_ui_tid, 0, 0);
    klog("KEYINJECT smp done\n");
    _exit(0);
    return 0;
}

__attribute__((constructor)) static void init(void){
    if(!is_app()) return;
    klog("KEYINJECT v6 loaded\n");

    pthread_t t; pthread_create(&t,0,thread,0);
}
