/* M2/E10 IPC probe v4: intercept queue REGISTRATION (cold path) + id allocation.
 * ABI-transparent naked stubs; logs each event with a monotonic timestamp.
 * Args: r0-r3 saved; native is called from C with the saved registers;
 *       its return value is written over the saved r0 slot before popping. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_kfd = -1;
static long g_t0;

static void out(const char *s, int n)
{
    if (g_kfd < 0) g_kfd = open("/dev/kmsg", O_WRONLY | O_APPEND);
    if (g_kfd >= 0 && write(g_kfd, s, n) == n) return;
    write(2, s, n);
}
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!g_t0) g_t0 = ts.tv_sec;
    return (ts.tv_sec - g_t0) * 1000 + ts.tv_nsec / 1000000;
}
static void *resolve(const char *name)
{
    void *h = dlopen("/usr/lib/libosal_uipc.so", RTLD_LAZY);
    void *p = h ? dlsym(h, name) : 0;
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    return p;
}
static void logk(const char *tag, unsigned qid, unsigned cb)
{
    char b[160];
    int n = snprintf(b, sizeof b, "M2Q pid=%d t=%ldms %s qid=%08x arg1=%08x\n",
                     (int)getpid(), now_ms(), tag, qid, cb);
    out(b, n);
}
#define CALLN(fn, regs) ((int (*)(unsigned, unsigned, unsigned, unsigned))(fn)) \
                        ((regs)[0], (regs)[1], (regs)[2], (regs)[3])

static void *n_reg, *n_regs, *n_dyn, *n_sub, *n_rcv, *n_snd;

int m2_h_reg(unsigned *r)   { if (!n_reg)  n_reg  = resolve("osal_reg_msg_queue_cb");      logk("REG",  r[0], r[1]); return n_reg  ? CALLN(n_reg, r)  : -1; }
int m2_h_regs(unsigned *r)  { if (!n_regs) n_regs = resolve("osal_reg_sync_msg_queue_cb"); logk("REGS", r[0], r[1]); return n_regs ? CALLN(n_regs, r) : -1; }
int m2_h_dyn(unsigned *r)   { if (!n_dyn)  n_dyn  = resolve("osal_dynamic_id");            int v = n_dyn ? CALLN(n_dyn, r) : -1; logk("DYN", (unsigned)v, r[0]); return v; }
int m2_h_sub(unsigned *r)   { if (!n_sub)  n_sub  = resolve("osal_subsystem_id");          int v = n_sub ? CALLN(n_sub, r) : -1; logk("SUB", (unsigned)v, r[0]); return v; }
int m2_h_rcv(unsigned *r)   { if (!n_rcv)  n_rcv  = resolve("osal_rcv_msg_tmo");          return n_rcv ? CALLN(n_rcv, r) : -1; }
int m2_h_snd(unsigned *r)   { if (!n_snd)  n_snd  = resolve("osal_snd_sync_msg");         return n_snd ? CALLN(n_snd, r) : -1; }

#define STUB(name, helper) \
__attribute__((naked)) int name(void) { \
    __asm__ volatile("push {r0,r1,r2,r3,r4,lr}\n mov r0,sp\n bl " #helper "\n" \
                     "str r0,[sp]\n pop {r0,r1,r2,r3,r4,lr}\n bx lr\n"); }

STUB(osal_reg_msg_queue_cb,      m2_h_reg)
STUB(osal_reg_sync_msg_queue_cb, m2_h_regs)
STUB(osal_dynamic_id,            m2_h_dyn)
STUB(osal_subsystem_id,          m2_h_sub)
STUB(osal_rcv_msg_tmo,           m2_h_rcv)
STUB(osal_snd_sync_msg,          m2_h_snd)

__attribute__((constructor)) static void init(void)
{
    char cmd[256] = {0};
    int f = open("/proc/self/cmdline", O_RDONLY), n;
    if (f >= 0) { n = read(f, cmd, sizeof cmd - 1); close(f); if (n > 0) cmd[n] = 0; }
    if (!strstr(cmd, "bootin") && !strstr(cmd, "im.elf")) return;
    n_reg  = resolve("osal_reg_msg_queue_cb");
    n_regs = resolve("osal_reg_sync_msg_queue_cb");
    n_dyn  = resolve("osal_dynamic_id");
    n_sub  = resolve("osal_subsystem_id");
    n_rcv  = resolve("osal_rcv_msg_tmo");
    n_snd  = resolve("osal_snd_sync_msg");
    char b[128];
    n = snprintf(b, sizeof b, "M2INIT pid=%d t=%ldms cmd=%.40s\n", (int)getpid(), now_ms(), cmd);
    out(b, n);
    n = snprintf(b, sizeof b, "M2PTR %p %p %p %p %p %p\n", n_reg, n_regs, n_dyn, n_sub, n_rcv, n_snd);
    out(b, n);
}
