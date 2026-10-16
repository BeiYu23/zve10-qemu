/* QEMU 诊断用：哨兵字节分配器（interposition 版 LD_PRELOAD）
 *
 * 目的：找出**谁越界写了堆**。
 * 原理：malloc 多要 16B 头 + GUARD 字节尾哨兵；free/realloc 校验哨兵，
 *       被破坏就打印「哪个块、多大」。
 *
 * 关键实现要点（踩过的坑）：
 *   1. **不要在 malloc 里调 dlsym** —— dlsym 自身可能分配内存 ⇒ 递归/崩溃。
 *      直接用 glibc 导出的 __libc_malloc / __libc_free / __libc_realloc 系列。
 *   2. 用 __progname 过滤，只对目标程序启用；其它程序完全透传。
 *   3. 哨兵用 32 位（ARM 32 位下 64 位常量会被截断）。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void  __libc_free(void *);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_memalign(size_t, size_t);
extern char *__progname;
#include <stdarg.h>
#include <sys/syscall.h>
#include <errno.h>

#define HDR   16
#define GUARD 32
#define MAGIC 0x57415244U
#define CANARY 0xDEADBEEFUL

static int enabled = 0;

static int name_match(const char *n, const char *want)
{
    if (!n) return 0;
    while (*n) {
        const char *a = n, *b = want;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
        n++;
    }
    return 0;
}

__attribute__((constructor))
static void guard_init(void)
{
    enabled = name_match(__progname, "im.elf");
    if (enabled) write(2, "[GUARD] enabled for im.elf\n", 28);
}

static inline void fill(unsigned char *p, size_t n)
{
    *(size_t *)(p)     = n;
    *(size_t *)(p + 8) = MAGIC;
    *(unsigned int *)(p + HDR + n) = (unsigned int)CANARY;
    memset(p + HDR + n + 8, 0xCD, GUARD - 8);
}

void *malloc(size_t n)
{
    if (!enabled) return __libc_malloc(n);
    unsigned char *p = __libc_malloc(n + HDR + GUARD);
    if (!p) return 0;
    fill(p, n);
    return p + HDR;
}

void *calloc(size_t a, size_t b)
{
    if (!enabled) return __libc_calloc(a, b);
    size_t n = a * b;
    unsigned char *p = __libc_malloc(n + HDR + GUARD);
    if (!p) return 0;
    fill(p, n);
    memset(p + HDR, 0, n);
    return p + HDR;
}

void *realloc(void *q, size_t n)
{
    if (!enabled) return __libc_realloc(q, n);
    if (!q) return malloc(n);
    unsigned char *p = (unsigned char *)q - HDR;
    if (*(size_t *)(p + 8) != MAGIC) return __libc_realloc(q, n);
    size_t old = *(size_t *)p;
    if (*(unsigned int *)(p + HDR + old) != (unsigned int)CANARY) {
        char buf[128];
        int k = snprintf(buf, sizeof(buf),
                         "[GUARD] OVERFLOW-DETECTED(realloc) ptr=%p size=%lu\n",
                         q, (unsigned long)old);
        if (k > 0) write(2, buf, k);
    }
    unsigned char *np = __libc_realloc(p, n + HDR + GUARD);
    if (!np) return 0;
    fill(np, n);
    return np + HDR;
}

void free(void *q)
{
    if (!q) return;
    if (!enabled) { __libc_free(q); return; }
    unsigned char *p = (unsigned char *)q - HDR;
    if (*(size_t *)(p + 8) != MAGIC) { __libc_free(q); return; }
    size_t n = *(size_t *)p;
    if (*(unsigned int *)(p + HDR + n) != (unsigned int)CANARY) {
        char buf[128];
        int k = snprintf(buf, sizeof(buf),
                         "[GUARD] OVERFLOW-DETECTED ptr=%p size=%lu\n",
                         q, (unsigned long)n);
        if (k > 0) write(2, buf, k);
    }
    __libc_free(p);
}

/* ---- ioctl 拦截：dump 内核回写的内容（找 UIPC 响应里的污染值）----
 * 用 syscall() 直接进内核，避免与真实 ioctl 递归。 */
int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;
    long r;

    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);

    errno = 0;
    r = syscall(SYS_ioctl, fd, req, arg);
    { int e = errno; (void)e; }

    if (enabled && arg && (req & 0xff000000UL) == 0x80000000UL) {
        unsigned char *b = (unsigned char *)arg;
        char buf[256];
        int k = 0, i;
        k += snprintf(buf + k, sizeof(buf) - k,
                      "[IOC] fd=%d req=0x%lx ret=%ld errno=%d d=", fd, req, r, errno);
        for (i = 0; i < 24 && k < (int)sizeof(buf) - 4; i++)
            k += snprintf(buf + k, sizeof(buf) - k, "%02x", b[i]);
        k += snprintf(buf + k, sizeof(buf) - k, "\n");
        if (k > 0) write(2, buf, (k > (int)sizeof(buf)) ? (int)sizeof(buf) : k);
    }
    return (int)r;
}
