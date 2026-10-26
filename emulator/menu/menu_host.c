/* Menu-only experiment: run original MWF + libObj entry, not im/IMDB main.
 * Firmware routines, object ownership and drawing remain original.
 * This is an experiment, NOT a menu implementation or success shim.
 */
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern int printf(const char *, ...);
extern int fflush(void *);
extern unsigned sleep(unsigned);
extern int usleep(unsigned);
extern long write(int, const void *, unsigned);
#define NEXT ((void *)-1)
#define GLOBAL_NOW 0x102
extern int open(const char *, int, ...);
extern int close(int);
extern void *mmap(void *, unsigned long, int, int, int, unsigned long);
extern void *mmap64(void *, unsigned long, int, int, int, long long);
#define MAP_SHARED_FIXED 0x11
#define PROT_RW 3
#define DDR0_SIZE 0x40000000u
#define DDR1_BASE 0x80000000u
#define DDR1_SIZE 0x10000000u
#define VA_DDR0 0x60000000u
#define VA_DDR1 0xa0000000u
static int phys_map_status;
static int map_phys_ranges(void) {
    int fd=open("/dev/mem",2); if(fd<0) return -1;
    void *a=mmap((void *)(unsigned long)VA_DDR0,DDR0_SIZE,PROT_RW,MAP_SHARED_FIXED,fd,0);
    void *b=mmap64((void *)(unsigned long)VA_DDR1,DDR1_SIZE,PROT_RW,MAP_SHARED_FIXED,fd,0x80000000LL);
    close(fd);
    if(a!=(void *)(unsigned long)VA_DDR0 || b!=(void *)(unsigned long)VA_DDR1) return -1;
    return 0;
}
/* Firmware platform helper: map physical DDR into the Linux ARM user alias.
 * This is an actual shared /dev/mem mapping, not a fabricated address reply. */
unsigned long arch_phys_to_cache(unsigned long pa) {
    if(pa<DDR0_SIZE) {
        if(phys_map_status==0) phys_map_status=map_phys_ranges()? -1:1;
        if(phys_map_status==1) return (unsigned long)pa+VA_DDR0;
    } else if(pa>=DDR1_BASE && pa<DDR1_BASE+DDR1_SIZE) {
        if(phys_map_status==0) phys_map_status=map_phys_ranges()? -1:1;
        if(phys_map_status==1) return (unsigned long)pa-DDR1_BASE+VA_DDR1;
    }
    printf("MENUHOST arch_phys_to_cache rejected pa=%08lx status=%d\\n",pa,phys_map_status); fflush(0);
    return 0;
}
static int strstr_(const char *h, const char *n)
{
    int i, j;
    for(i = 0; h[i]; i++) { for(j = 0; n[j]; j++) if(h[i+j] != n[j]) break; if(!n[j]) return 1; }
    return 0;
}
static void mark(const char *s) { unsigned n=0; while(s[n]) ++n; write(1,s,n); }
static void *load_flags(const char *s, int flags) {
    printf("MENUHOST dlopen %s flags=%x\n",s,flags); fflush(0);
    void *p=dlopen(s,flags);
    printf("MENUHOST handle=%p error=%s\n",p,p?"none":dlerror()); fflush(0);
    return p;
}
static void *load(const char *s) { return load_flags(s,GLOBAL_NOW); }
/* The only virtual configuration: documented reserved monitor/display areas.
 * Unknown requests fail explicitly; no invented config tables or fake writes.
 * This executable deliberately replaces the CP Backup transport for these keys.
 */
int osal_snd_sync_msg(unsigned qid, void *msg) {
    static int (*real)(unsigned,void *);
    if(qid==0x003e014d && msg) {
        unsigned *m=msg, value=0, key=m[6]; int known=1;
        switch(key) {
        case 0x00fa0028: value=0x8f000000; break;
        case 0x00fa005d: value=0x00080000; break;
        case 0x00fa00a3: value=0x20312000; break;
        case 0x00fa00a4: value=0x00100000; break;
        default: known=0; break;
        }
        printf("MENUHOST backup count=%u key=%08x supported=%d value=%08x\n",m[2],key,known,value); fflush(0);
        if(!known) return -1024;
        m[1]=value; return 0;
    }
    if(!real) real=(int(*)(unsigned,void *))dlsym(NEXT,"osal_snd_sync_msg");
    return real?real(qid,msg):-1024;
}
/* Observe (never alter) the framework thread's blocking point. */
static int rcv_seen[32];
static int rcv_n;
int osal_rcv_msg_tmo(unsigned qid, void **msg, long tmo)
{
    static int (*real)(unsigned,void**,long);
    if(!real) real=(int(*)(unsigned,void**,long))dlsym(NEXT,"osal_rcv_msg_tmo");
    int r = real ? real(qid,msg,tmo) : -1;
    int i, known = 0;
    for(i=0;i<rcv_n;i++) if((unsigned)rcv_seen[i]==qid) { known = 1; break; }
    if(!known && rcv_n < 32) {
        rcv_seen[rcv_n++] = (int)qid;
        printf("MENUHOST RCV new qid=%08x tmo=%ld ret=%d\n",qid,tmo,r); fflush(0);
    }
    return r;
}

/* Dump where every thread of this process is blocked, using the kernel's
 * /proc/self/task/<tid>/wchan. Read-only diagnosis; alters nothing. */
extern long syscall(long, ...);
extern int read(int, void *, unsigned long);
extern int snprintf(char *, unsigned long, const char *, ...);
#define AT_FDCWD_ 0
static unsigned libobj_base;
static void dump_libobj_map(void)
{
    char buf[24000]; int fd = open("/proc/self/maps", 0); int k;
    if(fd < 0) return;
    k = (int)read(fd, buf, sizeof(buf)-1); close(fd);
    if(k <= 0) return;
    buf[k] = 0;
    char *p = buf;
    while(*p) {
        char *e = p; while(*e && *e != '\n') e++;
        char c = *e; *e = 0;
        if(strstr_(p, "libObj.so") && strstr_(p, "r-x")) {
            unsigned v = 0; int ok = 0;
            for(int i = 0; i < 8; i++) {
                char c = p[i]; int d = -1;
                if(c >= '0' && c <= '9') d = c - '0';
                else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
                if(d < 0) { ok = 0; break; }
                v = v * 16 + d; ok = 1;
            }
            if(ok) libobj_base = v;
            printf("MENUHOST LIBOBJ %s (base %08x)\n", p, libobj_base); fflush(0);
        }
        if(strstr_(p, "libc-2.11.2.so") && strstr_(p, "r-x")) {
            printf("MENUHOST LIBC %s\n", p); fflush(0);
        }
        if(strstr_(p, "libosal_utm.so") && strstr_(p, "r-x")) {
            printf("MENUHOST UTM %s\n", p); fflush(0);
        }
        if(strstr_(p, "libosal_uipc.so") && strstr_(p, "r-x")) {
            printf("MENUHOST UIPC %s\n", p); fflush(0);
        }
        *e = c; if(!c) break; p = e + 1;
    }
}

static void dump_tasks(const char *when)
{
    char buf[2048];
    int fd = open("/proc/self/task", 0);
    if(fd < 0) { printf("MENUHOST TASKS %s: cannot open\n", when); fflush(0); return; }
    long n = syscall(217 /*getdents64*/, fd, buf, sizeof(buf));
    close(fd);
    if(n <= 0) {
        printf("MENUHOST TASKS %s: getdents failed, probing tids\n", when); fflush(0);
        for(long t = 100; t < 400; ++t) {
            char p[64]; int f, k; char comm[32];
            snprintf(p,sizeof p,"/proc/self/task/%ld/comm",t);
            f = open(p,0); if(f < 0) continue;
            k = (int)read(f,comm,sizeof(comm)-1); close(f);
            if(k <= 0) continue;
            comm[k]=0; for(int j=0;j<k;j++) if(comm[j]=='\n') comm[j]=0;
            char wchan[64]="?"; snprintf(p,sizeof p,"/proc/self/task/%ld/wchan",t);
            f = open(p,0); if(f>=0){ k=(int)read(f,wchan,sizeof(wchan)-1); close(f); if(k>0){wchan[k]=0; for(int j=0;j<k;j++) if(wchan[j]=='\n') wchan[j]=0;} }
            printf("MENUHOST   tid=%-6ld comm=%-16s wchan=%s\n", t, comm, wchan); fflush(0);
        }
        return;
    }
    printf("MENUHOST TASKS %s:\n", when); fflush(0);
    for(long off = 0; off < n; ) {
        unsigned long ino; long roff; unsigned short reclen; char *name;
        ino = *(unsigned long *)(buf+off);
        roff = *(long *)(buf+off+8);
        reclen = *(unsigned short *)(buf+off+16);
        name = buf+off+19;
        (void)ino; (void)roff;
        if(name[0] >= '0' && name[0] <= '9') {
            char p[64]; int f; char comm[32] = "?"; char wchan[64] = "?";
            int k;
            snprintf(p,sizeof p,"/proc/self/task/%s/comm",name);
            f = open(p,0); if(f>=0) { k = (int)read(f,comm,sizeof(comm)-1); if(k>0){comm[k]=0; for(int j=0;j<k;j++) if(comm[j]=='\n') comm[j]=0;} close(f); }
            snprintf(p,sizeof p,"/proc/self/task/%s/wchan",name);
            f = open(p,0); if(f>=0) { k = (int)read(f,wchan,sizeof(wchan)-1); if(k>0){wchan[k]=0; for(int j=0;j<k;j++) if(wchan[j]=='\n') wchan[j]=0;} close(f); }
            {
                char sp[160]; int sf, sk; unsigned long pc = 0;
                char st[512];
                snprintf(sp,sizeof sp,"/proc/self/task/%s/stat",name);
                sf = open(sp,0);
                if(sf >= 0) {
                    sk = (int)read(sf, st, sizeof(st)-1); close(sf);
                    if(sk > 0) {
                        st[sk] = 0;
                        char *rp = st;
                        while(*rp && *rp != ')') rp++;   /* skip pid (comm) */
                        if(*rp == ')') {
                            rp++;
                            int f = 3;                   /* next field number */
                            char *tok = rp;
                            while(*tok && f < 31) {
                                while(*tok == ' ') tok++;
                                if(!*tok) break;
                                char *end = tok; while(*end && *end != ' ') end++;
                                if(f == 30) { /* kstkeip */
                                    unsigned long v = 0; int base = 10;
                                    char *q = tok;
                                    if(q[0]=='0' && (q[1]=='x'||q[1]=='X')) { base = 16; q += 2; }
                                    while(*q && q < end) {
                                        int d = -1;
                                        if(*q>='0'&&*q<='9') d = *q-'0';
                                        else if(base==16 && *q>='a'&&*q<='f') d = *q-'a'+10;
                                        if(d < 0) break;
                                        v = v*base + d; q++;
                                    }
                                    pc = v;
                                    break;
                                }
                                tok = end; f++;
                            }
                        }
                    }
                }
                printf("MENUHOST   tid=%-6s comm=%-16s wchan=%-24s uPC=%08lx\n", name, comm, wchan, pc); fflush(0);
            }
        }
        off += reclen;
    }
}

/* Prove whether the MA request ever reaches a consumer: queue depth tells us
 * "not delivered" (depth grows, nobody reads) vs "consumed, no reply" (depth 0). */
static unsigned stat_q(unsigned qid)
{
    static int (*fn)(unsigned, unsigned *);
    unsigned st = 0;
    if(!fn) fn = (int(*)(unsigned,unsigned*))dlsym((void*)-1, "osal_stat_msgq");
    if(!fn) return 0xdeadbeefu;
    if(fn(qid, &st)) return 0xdeadbeeeu;
    return st;
}
static void probe_queues(const char *when)
{
    printf("MENUHOST QSTAT %s ma=%08x dmm=%08x log=%08x\n", when,
           stat_q(0x014c0424u), stat_q(0x0061003cu), stat_q(0x008f013au)); fflush(0);
}
static void dump_modules(void)
{
    char buf[3000]; int fd = open("/proc/modules",0); int k;
    if(fd < 0) { printf("MENUHOST MODULES unavailable\n"); fflush(0); return; }
    k = (int)read(fd, buf, sizeof(buf)-1); close(fd);
    if(k <= 0) return;
    buf[k] = 0;
    printf("MENUHOST MODULES>>\n"); fflush(0);
    char *p = buf;
    while(*p) {
        char *e = p; while(*e && *e != '\n') e++;
        char c = *e; *e = 0;
        if(strstr_(p, "grm_ma") || strstr_(p, "fake_gles2") || strstr_(p, "osal_uipc")) {
            printf("MENUHOST   %s\n", p); fflush(0);
        }
        *e = c; if(!c) break; p = e + 1;
    }
}

/* Observe the firmware's own thread creation: the descriptor's first words tell
 * us the real entry function that Application_System_Init asks the RTOS to run. */
static void *utm_handle;
static int   thread_calls;
static unsigned gettid_(void)
{
    return (unsigned)syscall(224 /*gettid*/);
}
static void log_desc(const char *who, const void *d)
{
    const unsigned *w = (const unsigned *)d;
    printf("MENUHOST %s tid=%u desc=%p words=%08x %08x %08x %08x %08x %08x %08x %08x\n",
           who, gettid_(), d, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]); fflush(0);
}
int osal_create_thread(void *desc)
{
    static int (*real)(void *);
    if(!real) real = (int(*)(void*))dlsym(utm_handle, "osal_create_thread");
    if(thread_calls < 8) { thread_calls++; log_desc("CREATETHREAD", desc); }
    int r = real ? real(desc) : -1;
    printf("MENUHOST CREATETHREAD returned=%d\n", r); fflush(0);
    return r;
}
long osal_create_waitable_thread(void *desc, unsigned flags)
{
    static long (*real)(void*,unsigned);
    if(!real) real = (long(*)(void*,unsigned))dlsym(utm_handle, "osal_create_waitable_thread");
    if(thread_calls < 8) { thread_calls++; log_desc("CREATEWAIT", desc); }
    long r = real ? real(desc, flags) : -1;
    printf("MENUHOST CREATEWAIT returned=%d\n", (int)r); fflush(0);
    return r;
}

/* Sample every thread's user PC at high frequency right after startup, while
 * the new threads are still executing their initialisation code. */
static unsigned task_pc(const char *tid)
{
    char sp[64]; char st[512]; int f, k; unsigned long pc = 0;
    snprintf(sp,sizeof sp,"/proc/self/task/%s/stat",tid);
    f = open(sp,0); if(f < 0) return 0;
    k = (int)read(f, st, sizeof(st)-1); close(f);
    if(k <= 0) return 0;
    st[k] = 0;
    char *rp = st;
    while(*rp && *rp != ')') rp++;
    if(*rp != ')') return 0;
    rp++;
    int fld = 3; char *tok = rp;
    while(*tok && fld < 31) {
        while(*tok == ' ') tok++;
        if(!*tok) break;
        char *end = tok; while(*end && *end != ' ') end++;
        if(fld == 30) {
            unsigned long v = 0; int b = 10; char *q = tok;
            if(q[0]=='0' && (q[1]=='x'||q[1]=='X')) { b = 16; q += 2; }
            while(*q && q < end) { int d = -1;
                if(*q>='0'&&*q<='9') d = *q-'0';
                else if(b==16 && *q>='a'&&*q<='f') d = *q-'a'+10;
                if(d<0) break; v = v*b + d; q++; }
            pc = v; break;
        }
        tok = end; fld++;
    }
    return (unsigned)pc;
}
static void sample_threads(void)
{
    static unsigned seen[64]; static char seen_tid[64][8]; int seen_n = 0;
    char buf[1024]; int fd = open("/proc/self/task", 0); long n;
    if(fd < 0) return;
    n = syscall(217, fd, buf, sizeof(buf)); close(fd);
    if(n <= 0) return;
    for(long off = 0; off < n; ) {
        unsigned short reclen = *(unsigned short *)(buf+off+16);
        char *name = buf+off+19;
        if(name[0] >= '0' && name[0] <= '9') {
            unsigned pc = task_pc(name);
            int i, known = 0;
            for(i = 0; i < seen_n; i++) if(seen[i] == pc) { known = 1; break; }
            if(!known && seen_n < 64) {
                seen[seen_n] = pc;
                for(int j = 0; j < 7 && name[j]; j++) seen_tid[seen_n][j] = name[j];
                seen_tid[seen_n][7] = 0;
                printf("MENUHOST SAMPLE tid=%s pc=%08x\n", seen_tid[seen_n], pc); fflush(0);
                seen_n++;
            }
        }
        off += reclen;
    }
}

/* Observe semaphore waits/posts: dmp_allocator_lib_init blocks on osal_wai_sem_tmo
 * with an infinite timeout, so the semaphore id and who posts it are decisive. */
static void *uipc_handle;
static int sem_n, sem_n2;
int osal_wai_sem_tmo(unsigned sem, long tmo)
{
    static int (*real)(unsigned,long);
    if(!real) real = (int(*)(unsigned,long))dlsym(uipc_handle, "osal_wai_sem_tmo");
    if(sem_n < 12) { sem_n++; printf("MENUHOST WAITSEM tid=%u sem=%08x tmo=%ld\n", gettid_(), sem, tmo); fflush(0); }
    int r = real ? real(sem, tmo) : -1;
    if(sem_n <= 12) { printf("MENUHOST WAITSEM returned=%d sem=%08x\n", r, sem); fflush(0); }
    return r;
}
int osal_sig_sem(unsigned sem)
{
    static int (*real)(unsigned);
    if(!real) real = (int(*)(unsigned))dlsym(uipc_handle, "osal_sig_sem");
    if(sem_n2 < 12) { sem_n2++; printf("MENUHOST SIGSEM tid=%u sem=%08x\n", gettid_(), sem); fflush(0); }
    return real ? real(sem) : -1;
}

/* The firmware's own app-thread body (libObj+0x8a2398):
 *   uxengine init -> construct the app object on this thread's stack
 *   -> post requestModelExecute -> run the app main loop -> destruct.
 * Running it in our own thread reproduces the original lifecycle exactly;
 * the object lives on that thread's stack, so no global is fabricated. */
static void *app_thread(void *arg)
{
    (void)arg;
    void (*body)(void) = (void(*)(void))(libobj_base + 0x8a2398 + 1);
    printf("MENUHOST app body enter %p\n", (void*)body); fflush(0);
    body();
    printf("MENUHOST app body returned\n"); fflush(0);
    return 0;
}

int main(int argc,char **argv) {
    (void)argc;(void)argv;
    mark("MENUHOST main: original im/IMDB main bypassed\n");
    void *uipc=load("/usr/lib/libosal_uipc.so"); if(!uipc) return 10; uipc_handle = uipc;
    void *utm=load("/usr/lib/libosal_utm.so"); if(!utm) return 10; utm_handle = utm;
    void *backup=load("/usr/lib/libbackup.so"); if(!backup) return 10;
    void *mwf=load("/usr/lib/libMWF.so"); if(!mwf) return 11;
    void (*mwfi)(void)=(void(*)(void))dlsym(mwf,"_ZN3MWF10InitializeEv");
    if(!mwfi) {mark("MENUHOST missing MWF Initialize\n"); return 12;}
    mark("MENUHOST MWF Initialize enter\n"); mwfi(); mark("MENUHOST MWF Initialize returned\n");
    dump_modules();
    void *obj=load_flags("/usr/lib/libObj.so",0x101); if(!obj) return 13;
    dump_libobj_map();
    void (*app)(const char *)=(void(*)(const char *))dlsym(obj,"Application_System_Init");
    unsigned *pvm=(unsigned *)dlsym(obj,"pViewManager");
    printf("MENUHOST API=%p pVMslot=%p\n",(void*)app,(void*)pvm); fflush(0);
    if(!app||!pvm) return 14;
    mark("MENUHOST Application_System_Init enter\n"); app(0); mark("MENUHOST Application_System_Init returned\n");
    /* Drive the app's own configuration initializer: initializeConfig(void) allocates
     * its config object and internally dlopens+initialises the model/view config
     * modules. This is original firmware code doing the original work. */
    for(int si = 0; si < 200; si++) { usleep(10000); sample_threads(); }
    {
        void *pth = load("/usr/lib/libpthread-2.11.2.so");
        int (*pcreate)(unsigned long *, const void *, void *(*)(void *), void *) =
            (int(*)(unsigned long *, const void *, void *(*)(void *), void *))dlsym(pth, "pthread_create");
        unsigned long th = 0;
        printf("MENUHOST launch app body via pthread_create=%p\n", (void*)pcreate); fflush(0);
        if(pcreate) { int rc = pcreate(&th, 0, app_thread, 0); printf("MENUHOST pthread_create rc=%d th=%lu\n", rc, th); fflush(0); }
    }
    /* Optional original entry points are disabled for this run: calling them in
     * this order faults on prerequisites the boot sequence would have created
     * (see REALCAM_GROUND_TRUTH 94/95). Thread-creation hooks stay enabled. */
    /* The original framework runs in its own thread created by
     * Application_System_Init. Returning from main would tear the process down
     * while that thread is still issuing UIPC traffic, which is what produced
     * the loader fault at the end of the previous run. Stay alive instead. */
    for(unsigned i=0;;++i) {
        sleep(1);
        if((i % 5)==0) { printf("MENUHOST tick=%u pVM=%08x\n",i,*pvm); fflush(0); }
        if(i==20) dump_tasks("t20");
    }
}
