/* v5: rcv/snd (as v3) + ONE new interception: osal_reg_msg_queue_cb.
 * Registration happens during library init, possibly BEFORE this lib's ctor runs,
 * so resolve lazily inside the stub via dlsym(RTLD_NEXT) (safe early), and if that
 * fails return 0 (benign for reg) instead of -1. Log qid + return address once. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <link.h>
#include <pthread.h>

static int g_kfd = -1;
static void out(const char *s, int n)
{
    if (g_kfd < 0) g_kfd = open("/dev/kmsg", O_WRONLY | O_APPEND);
    if (g_kfd >= 0 && write(g_kfd, s, n) == n) return;
    write(2, s, n);
}
static void *n_reg, *n_regs, *n_rcv, *n_snd;
static int reg_logged;
static unsigned long libobj_base(void);
static void read_singleton(void);

int m2_h_reg(unsigned *r)          /* r[0]=qid r[1]=cb r[4]=r4 r[5]=lr */
{
    static int nreg;
    if (++nreg == 40) read_singleton();
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
static unsigned long libobj_base(void);
static void gi_fire(void);
void m2_h_res_rcv(unsigned *r) { static int calls; if (!n_rcv) { n_rcv = dlsym(RTLD_NEXT, "osal_rcv_msg_tmo");
    if (!n_rcv) { void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY); n_rcv = h ? dlsym(h, "osal_rcv_msg_tmo") : 0; } }
    if (++calls == 400) gi_fire(); }
void m2_h_res_snd(unsigned *r) { if (!n_snd) { n_snd = dlsym(RTLD_NEXT, "osal_snd_sync_msg");
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

static void read_singleton(void)
{
    unsigned long b = libobj_base();
    char buf[160]; int n;
    if (!b) { out("M2SINGLE nobase\n", 16); return; }
    unsigned long *g = (unsigned long *)(b + 0x3d7214UL);
    unsigned long v = *g;
    n = snprintf(buf, sizeof buf, "M2SINGLE base=%08x addr=%08x val=%08x\n",
                 (unsigned)b, (unsigned)(b + 0x3d7214UL), (unsigned)v);
    out(buf, n);
}

static unsigned long libobj_base(void)
{
    static unsigned long b;
    if (b) return b;
    struct { unsigned long v; } out = {0};
    int cb(struct dl_phdr_info *i, size_t sz, void *d) {
        (void)sz; (void)d;
        const char *n = i->dlpi_name ? i->dlpi_name : "";
        if (strstr(n, "libObj")) { out.v = i->dlpi_addr; return 1; }
        return 0;
    }
    dl_iterate_phdr(cb, 0);
    b = out.v;
    return b;
}
static void *call_gi(void *a)
{
    (void)a;
    unsigned long b = libobj_base();
    char buf[128]; int n = snprintf(buf, sizeof buf, "M2GI base=%08x\n", (unsigned)b);
    out(buf, n);
    if (!b) return 0;
    void *(*gi)(void) = (void *(*)(void))(b + 0x208005 + 1);
    void *r = gi();
    n = snprintf(buf, sizeof buf, "M2GI ret=%08x\n", (unsigned)(unsigned long)r);
    out(buf, n);
    return 0;
}
static void *gi_worker(void *a)
{
    (void)a;
    sleep(15);
    unsigned long b = libobj_base();
    char buf[128]; int n = snprintf(buf, sizeof buf, "M2GI call base=%08x\n", (unsigned)b);
    out(buf, n);
    if (!b) return 0;
    void *(*gi)(void) = (void *(*)(void))(b + 0x208005 + 1);
    void *r = gi();
    n = snprintf(buf, sizeof buf, "M2GI ret=%08x\n", (unsigned)(unsigned long)r);
    out(buf, n);
    return 0;
}

static void *reader2(void *a)
{
    (void)a;
    int fd = open("/dev/ttyAM1", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    for (;;) {
        char b[128]; int n;
        while ((n = read(fd, b, sizeof b - 1)) > 0) {
            b[n] = 0;
            if (strstr(b, "#GI")) { pthread_t t; pthread_create(&t, 0, call_gi, 0); }
        }
        usleep(100000);
    }
    return 0;
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
        pthread_t t; pthread_create(&t, 0, reader2, 0);
        pthread_t g; pthread_create(&g, 0, gi_worker, 0);   /* lazy-singleton call */
    }
}
