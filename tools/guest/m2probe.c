/* v5: rcv/snd (as v3) + ONE new interception: osal_reg_msg_queue_cb.
 * Registration happens during library init, possibly BEFORE this lib's ctor runs,
 * so resolve lazily inside the stub via dlsym(RTLD_NEXT) (safe early), and if that
 * fails return 0 (benign for reg) instead of -1. Log qid + return address once. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <link.h>
#include <dirent.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <pthread.h>

static int g_kfd = -1;
static void out(const char *s, int n)
{
    if (g_kfd < 0) g_kfd = open("/dev/kmsg", O_WRONLY | O_APPEND);
    if (g_kfd >= 0 && write(g_kfd, s, n) == n) return;
    write(2, s, n);
}
static void *n_reg, *n_regs, *n_rcv, *n_snd, *n_sd, *n_dl;
static int reg_logged;
static unsigned long libobj_base(void) { return 0; }
static void gi_fire(void);


static void log_lib(const char *tag, unsigned qid, unsigned *r)
{
    Dl_info di;
    unsigned long lr = (unsigned long)r[5] & ~1UL;
    if (dladdr((void *)lr, &di) && di.dli_fname) {
        char b[220];
        int n = snprintf(b, sizeof b, "M2LIB %s qid=%08x lib=%s off=%08lx\n",
                         tag, qid, di.dli_fname, lr - (unsigned long)di.dli_fbase);
        out(b, n);
    }
}

int m2_h_reg(unsigned *r)          /* r[0]=qid r[1]=cb r[4]=r4 r[5]=lr */
{
    static int nreg;
    if (nreg == 100) m2_dump_stuck();
    if (!n_reg) {                   /* lazy, early-safe */
        n_reg = dlsym(RTLD_NEXT, "osal_reg_msg_queue_cb");
        if (!n_reg) n_reg = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY)
                            ? dlsym(dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY), "osal_reg_msg_queue_cb") : 0;
    }
    if (!n_reg) {
        if (!reg_logged) {
            char b[120]; int n = snprintf(b, sizeof b,
                "M2REGNULL pid=%d qid=%08x lr=%08x r4=%08x\n", (int)getpid(), r[0], r[5], r[4]);
            out(b, n); reg_logged = 1;
        }
        return 0;                   /* benign: reg returns 0 = success */
    }
    log_lib("REG", r[0], r);
    {
        char b[120]; int n = snprintf(b, sizeof b,
            "M2REG pid=%d qid=%08x lr=%08x r4=%08x\n", (int)getpid(), r[0], r[5], r[4]);
        out(b, n);
    }
    return ((int (*)(unsigned, unsigned, unsigned, unsigned))n_reg)(r[0], r[1], r[2], r[3]);
}

static int regs_logged;
int m2_h_regs(unsigned *r)
{
    if (!n_regs) {
        n_regs = dlsym(RTLD_NEXT, "osal_reg_sync_msg_queue_cb");
        if (!n_regs) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY);
                       n_regs = h ? dlsym(h, "osal_reg_sync_msg_queue_cb") : 0; }
    }
    if (!n_regs) {
        if (!regs_logged) {
            char b[120]; int n = snprintf(b, sizeof b,
                "M2REGSYNULL pid=%d qid=%08x lr=%08x r4=%08x\n", (int)getpid(), r[0], r[5], r[4]);
            out(b, n); regs_logged = 1;
        }
        return 0;
    }
    log_lib("REGSY", r[0], r);
    {
        char b[120]; int n = snprintf(b, sizeof b,
            "M2REGSY pid=%d qid=%08x lr=%08x r4=%08x\n", (int)getpid(), r[0], r[5], r[4]);
        out(b, n);
    }
    return ((int (*)(unsigned, unsigned, unsigned, unsigned))n_regs)(r[0], r[1], r[2], r[3]);
}

__attribute__((naked)) int osal_reg_sync_msg_queue_cb(void)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_regs\n"
                     "str r0,[sp]\n pop {r0,r1,r2,r3,r4,lr}\n bx lr\n");
}

__attribute__((naked)) int osal_reg_msg_queue_cb(void)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_reg\n"
                     "str r0,[sp]\n pop {r0,r1,r2,r3,r4,lr}\n bx lr\n");
}

/* rcv / snd: exactly as v3 (tail-call native) */
#define NQ4 48
static unsigned rv_seen[NQ4], rv_n;
void m2_h_res_rcv(unsigned *r)
{
    int isnew = 1;
    for (unsigned i = 0; i < rv_n && i < NQ4; i++) if (rv_seen[i] == r[0]) { isnew = 0; break; }
    if (isnew && rv_n < NQ4) {
        rv_seen[rv_n++] = r[0];
        Dl_info di; unsigned long lr = (unsigned long)r[5] & ~1UL;
        if (dladdr((void *)lr, &di) && di.dli_fname) {
            char b[220]; int n = snprintf(b, sizeof b, "M2RCV qid=%08x lib=%s off=%08lx\n",
                                          r[0], di.dli_fname, lr - (unsigned long)di.dli_fbase);
            out(b, n);
        }
    }
    static int calls; if (!n_rcv) { n_rcv = dlsym(RTLD_NEXT, "osal_rcv_msg_tmo");
    if (!n_rcv) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY); n_rcv = h ? dlsym(h, "osal_rcv_msg_tmo") : 0; } }
    if (++calls == 400) gi_fire(); }
#define NSQ 32
static volatile unsigned sq_q[NSQ], sq_e[NSQ], sq_r[NSQ], sq_c;
void m2_dump_stuck(void)
{
    char b[160];
    for (unsigned i = 0; i < sq_c && i < NSQ; i++) {
        int n = snprintf(b, sizeof b, "M2STUCK qid=%08x enter=%u ret=%u pending=%d\n",
                         sq_q[i], sq_e[i], sq_r[i], (int)(sq_e[i] - sq_r[i]));
        out(b, n);
    }
}
static int snd_idx(unsigned q)
{
    for (unsigned i = 0; i < sq_c && i < NSQ; i++) if (sq_q[i] == q) return (int)i;
    if (sq_c < NSQ) { unsigned i = sq_c++; sq_q[i] = q; return (int)i; }
    return -1;
}
void m2_h_res_snd2(unsigned *r)
{
    if (!n_snd) { n_snd = dlsym(RTLD_NEXT, "osal_snd_sync_msg");
        if (!n_snd) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY); n_snd = h ? dlsym(h, "osal_snd_sync_msg") : 0; } }
    int i = snd_idx(r[0]);
    if (i >= 0) sq_e[i]++;
}
void m2_h_ret_snd(unsigned *r, int ret)
{
    int i = snd_idx(r[0]);
    if (i >= 0) sq_r[i]++;
}
#define NSQ2 40
static unsigned snd_seen[NSQ2], snd_n;
void m2_h_res_snd(unsigned *r)
{
    int newq = 1;
    for (unsigned i = 0; i < snd_n && i < NSQ2; i++) if (snd_seen[i] == r[0]) { newq = 0; break; }
    if (newq && snd_n < NSQ2) {
        snd_seen[snd_n++] = r[0];
        Dl_info di; unsigned long lr = (unsigned long)r[5] & ~1UL;
        if (dladdr((void *)lr, &di) && di.dli_fname) {
            char b[220]; int n = snprintf(b, sizeof b,
                "M2SND qid=%08x lib=%s off=%08lx\n", r[0], di.dli_fname,
                lr - (unsigned long)di.dli_fbase);
            out(b, n);
        }
    }
    if (!n_snd) { n_snd = dlsym(RTLD_NEXT, "osal_snd_sync_msg");
    if (!n_snd) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY); n_snd = h ? dlsym(h, "osal_snd_sync_msg") : 0; } } }

static void gi_fire(void)
{
    static int done;
    if (done) return;
    done = 1;
    unsigned long b = libobj_base();
    char buf[128]; int n = snprintf(buf, sizeof buf, "M2GI fire base=%08x\n", (unsigned)b);
    out(buf, n);
    if (!b) return;
    void *(*gi)(void) = (void *(*)(void))(b + 0x208005 + 1);
    void *r = gi();
    n = snprintf(buf, sizeof buf, "M2GI ret=%08x\n", (unsigned)(unsigned long)r);
    out(buf, n);
}

__attribute__((naked)) int osal_rcv_msg_tmo(void)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_res_rcv\n"
                     "pop {r0,r1,r2,r3,r4,lr}\n ldr ip,=n_rcv\n ldr ip,[ip]\n"
                     "cmp ip,#0\n bxne ip\n mvn r0,#0\n bx lr\n");
}
__attribute__((naked)) int osal_snd_sync_msg(void)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_res_snd\n"
                     "pop {r0,r1,r2,r3,r4,lr}\n ldr ip,=n_snd\n ldr ip,[ip]\n"
                     "cmp ip,#0\n bxne ip\n mvn r0,#0\n bx lr\n");
}

#define NQ3 40
static unsigned sd_seen[NQ3], sd_n;
void m2_h_res_sd(unsigned *r)
{
    int isnew = 1;
    for (unsigned i = 0; i < sd_n && i < NQ3; i++) if (sd_seen[i] == r[0]) { isnew = 0; break; }
    if (isnew && sd_n < NQ3) {
        sd_seen[sd_n++] = r[0];
        char b[96]; int n = snprintf(b, sizeof b, "M2SD qid=%08x\n", r[0]);
        out(b, n);
    }
    if (r[0] == 0x90013d) {
        static unsigned cnt;
        if (++cnt <= 8) {
            unsigned char *m = (unsigned char *)r[1];
            char b[128]; int n = snprintf(b, sizeof b, "M2DOPS n=%u b16=%02x%02x b18=%02x%02x b20=%02x b21=%02x\n",
                                          cnt, m?m[17]:0, m?m[16]:0, m?m[19]:0, m?m[18]:0, m?m[20]:0, m?m[21]:0);
            out(b, n);
        }
    }
    if (!n_sd) { n_sd = dlsym(RTLD_NEXT, "osal_snd_sync_direct");
        if (!n_sd) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY); n_sd = h ? dlsym(h, "osal_snd_sync_direct") : 0; } }
}
__attribute__((naked)) int osal_snd_sync_direct(void)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_res_sd\n"
                     "pop {r0,r1,r2,r3,r4,lr}\n ldr ip,=n_sd\n ldr ip,[ip]\n"
                     "cmp ip,#0\n bxne ip\n mvn r0,#0\n bx lr\n");
}

static volatile pid_t dump_pid;
static void force_ui(void)
{
    char b[200];
    out("M2SIG enter\n", 13);
    void *mpr = dlopen("/usr/lib/libmpr.so", RTLD_LAZY | RTLD_GLOBAL);
    int n = snprintf(b, sizeof b, "M2SIG mpr=%p err=%s\n", mpr, mpr ? "none" : (dlerror() ? dlerror() : "?"));
    out(b, n);
    void *h = dlopen("/usr/lib/appFw.so", RTLD_LAZY | RTLD_GLOBAL);
    n = snprintf(b, sizeof b, "M2SIG appFw=%p err=%s\n", h, h ? "none" : (dlerror() ? dlerror() : "?"));
    out(b, n);
    if (!h) return;
    void *ap = dlsym(h, "Application_Start");
    n = snprintf(b, sizeof b, "M2SIG Application_Start=%p\n", ap);
    out(b, n);
    void *cw = dlsym(RTLD_DEFAULT, "osal_create_waitable_thread");
    if (!cw) { void *u = dlopen("/usr/lib/libosal_utm.so", RTLD_LAZY | RTLD_GLOBAL); cw = u ? dlsym(u, "osal_create_waitable_thread") : 0; }
    n = snprintf(b, sizeof b, "M2SIG create_waitable=%p\n", cw);
    out(b, n);
    if (!ap || !cw) return;
    struct { const char *name; int prio; int stack; void *fn; unsigned short flags; short pad; int a; int b; int c; } tp;
    memset(&tp, 0, sizeof tp);
    tp.name = "AppFw"; tp.prio = 4; tp.stack = 0x20000; tp.fn = ap; tp.flags = 0x1cf; tp.a = 1;
    int rc = ((int (*)(void *))cw)(&tp);
    n = snprintf(b, sizeof b, "M2SIG create rc=%d\n", rc);
    out(b, n);
}
static void sig_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si; (void)uc;
    static volatile int done;
    if (done) return;
    done = 1;
    force_ui();
}
static void *sig_waiter(void *a)
{
    (void)a;
    sleep(150);
    kill(dump_pid, SIGUSR1);
    return 0;
}

static void dump_maps(void)
{
    char path[96], buf[4096];
    pid_t pp = dump_pid;
    snprintf(path, sizeof path, "/proc/%d/maps", (int)pp);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { out("M2MAPS nodir\n", 12); return; }
    char seen[120][64]; int nseen = 0;
    int n; char rest[2048];
    while ((n = read(fd, buf, sizeof buf - 1)) > 0) {
        buf[n] = 0;
        char *save = 0, *line = strtok_r(buf, "\n", &save);
        while (line) {
            char *sl = strrchr(line, '/');
            if (sl && strstr(line, ".so")) {
                char *q = strchr(sl, '/'); (void)q;
                int dup = 0;
                for (int i = 0; i < nseen; i++) if (strcmp(seen[i], sl) == 0) { dup = 1; break; }
                if (!dup && nseen < 120) {
                    strncpy(seen[nseen++], sl, sizeof seen[0] - 1);
                    char o[128]; int m = snprintf(o, sizeof o, "M2MAPS %s\n", sl);
                    out(o, m);
                }
                (void)rest;
            }
            line = strtok_r(0, "\n", &save);
        }
    }
    close(fd);
}
static void dump_threads(void)
{
    char path[96], buf[256];
    pid_t pp = dump_pid;
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pp);
    { int t = open(path, O_RDONLY); if (t < 0) { char o[80]; int m = snprintf(o, sizeof o, "M2T parent_gone pid=%d\n", (int)pp); out(o, m); return; } close(t); }
    snprintf(path, sizeof path, "/proc/%d/task", (int)pp);
    DIR *d = opendir(path);
    if (!d) { out("M2T nodir\n", 10); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p2[128]; int fd;
        snprintf(p2, sizeof p2, "/proc/%d/task/%s/wchan", (int)pp, e->d_name);
        fd = open(p2, O_RDONLY); int n = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1;
        if (fd >= 0) close(fd);
        if (n > 0) buf[n] = 0; else buf[0] = 0;
        for (int i = 0; buf[i]; i++) if (buf[i] == '\n') buf[i] = 0;
        snprintf(p2, sizeof p2, "/proc/%d/task/%s/stat", (int)pp, e->d_name);
        char st[512]; fd = open(p2, O_RDONLY); int n2 = fd >= 0 ? read(fd, st, sizeof st - 1) : -1;
        if (fd >= 0) close(fd);
        char state = n2 > 0 ? st[n2] = 0, st[n2 > 0 ? (n2/1) : 0] : '?';
        char stt[2] = {'?', 0};
        if (n2 > 0) { char *q = strrchr(st, ')'); if (q && q[1] == ' ') stt[0] = q[2]; else stt[0] = '?'; }
        (void)state;
        char o[280]; int m = snprintf(o, sizeof o, "M2T tid=%s st=%c wchan=%s\n", e->d_name, stt[0], buf);
        out(o, m);
    }
    closedir(d);
}
static void *thread_dumper(void *a) { (void)a; sleep(50); dump_maps(); dump_threads(); return 0; }

#define NQ5 40
static unsigned char dl_logged[NQ5]; static char dl_names[NQ5][64]; static unsigned dl_n;
static int dl_guard;
void m2_h_dl(unsigned *r)
{
    const char *name = (const char *)r[0];
    if (!dl_guard && name) {
        dl_guard = 1;
        int seen = 0;
        for (unsigned i = 0; i < dl_n && i < NQ5; i++) if (strncmp(dl_names[i], name, 63) == 0) { seen = 1; break; }
        if (!seen && dl_n < NQ5) {
            strncpy(dl_names[dl_n], name, 63); dl_logged[dl_n] = 1; dl_n++;
            char b[160]; int n = snprintf(b, sizeof b, "M2DL name=%s\n", name);
            out(b, n);
        }
        dl_guard = 0;
    }
    if (!n_dl) { n_dl = dlsym(RTLD_NEXT, "dlopen");
        if (!n_dl) { void *h = dlopen("/lib/libdl.so.2", RTLD_LAZY); n_dl = h ? dlsym(h, "dlopen") : 0; } }
}
__attribute__((naked)) void *dlopen(const char *f, int fl)
{
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl m2_h_dl\n"
                     "pop {r0,r1,r2,r3,r4,lr}\n ldr ip,=n_dl\n ldr ip,[ip]\n"
                     "cmp ip,#0\n bxne ip\n mov r0,#0\n bx lr\n");
}

__attribute__((constructor)) static void init(void)
{
    char cmd[256] = {0};
    int f = open("/proc/self/cmdline", O_RDONLY), n;
    if (f >= 0) { n = read(f, cmd, sizeof cmd - 1); close(f); if (n > 0) cmd[n] = 0; }
    if (!strstr(cmd, "bootin") && !strstr(cmd, "im.elf")) return;
    char b[128];
    n = snprintf(b, sizeof b, "M2INIT pid=%d cmd=%.48s\n", (int)getpid(), cmd);
    out(b, n);
    if (strstr(cmd, "im.elf")) {
        { dump_pid = getpid();
          pid_t c = fork(); if (c == 0) { thread_dumper(0); _exit(0); }
          struct sigaction sa; memset(&sa, 0, sizeof sa);
          sa.sa_sigaction = sig_handler; sa.sa_flags = SA_SIGINFO;
          sigaction(SIGUSR1, &sa, 0);
          pid_t c2 = fork(); if (c2 == 0) { sig_waiter(0); _exit(0); } }
    }
}

/* ★ M2/libmprctrl ABI shim: provide the (unsigned long) variant that
   libmprctrl.so imports, forwarding to M2 libmpr's (unsigned int) version. */
extern int _ZN5MprIf34SetCreateParam_SupportContentTypesEj(unsigned int);
int _ZN5MprIf34SetCreateParam_SupportContentTypesEm(unsigned long v)
{
    static int once;
    if (!once) { once = 1; out("M2SHIM typesM called\n", 23); }
    return _ZN5MprIf34SetCreateParam_SupportContentTypesEj((unsigned int)v);
}
