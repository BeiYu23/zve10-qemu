/* QEMU 诊断用：释放后使用（use-after-free）检测 —— 隔离区 + 毒化
 *
 * 与 libguard（尾哨兵）互补：
 *   - 尾哨兵只能发现"写越过了块的末尾"
 *   - 本库把 free 掉的块**留在隔离区并毒化**，每次分配前抽查毒化字节
 *     ⇒ 能发现"写回了已释放的块"（UAF）
 *
 * 要点：
 *   - 用 __libc_malloc/__libc_free（不调 dlsym，避免递归）
 *   - 只对目标程序启用（__progname）
 *   - 隔离区有上限，超出后真正释放最旧的块
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/syscall.h>

extern void *__libc_malloc(size_t);
extern void  __libc_free(void *);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_calloc(size_t, size_t);
extern char *__progname;

#define HDR      16
#define MAGIC    0x51554152U          /* 'QUAR' */
#define POISON   0xDD
#define QUAR_MAX 60000                /* 隔离区上限（块数） */

static int enabled = 0;
static unsigned long n_free = 0, n_hit = 0, n_alloc = 0;

typedef struct { unsigned char *p; size_t n; } qent;
static qent q[QUAR_MAX];
static int qn = 0;

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
static void q_init(void)
{
    enabled = name_match(__progname, "im.elf");
    if (enabled) write(2, "[QUAR] enabled for im.elf\n", 26);
}

/* 抽查隔离区：毒化字节被改写 ⇒ 有人写了已释放的块 */
static void scan_quar(void)
{
    int i;
    for (i = 0; i < qn; i++) {
        unsigned char *p = q[i].p;
        size_t n = q[i].n;
        size_t k, bad = 0;
        for (k = 0; k < n && k < 256; k++) {      /* 抽查前 256 字节 */
            if (p[HDR + k] != POISON) { bad = k; break; }
        }
        if (bad || (n > 256 && p[HDR + n - 1] != POISON)) {
            char buf[128];
            int l = snprintf(buf, sizeof(buf),
                "[QUAR] USE-AFTER-FREE! blk=%p size=%lu off=%lu\n",
                p + HDR, (unsigned long)n, (unsigned long)(bad ? bad : n - 1));
            if (l > 0) write(2, buf, l);
            __sync_fetch_and_add(&n_hit, 1);
            q[i] = q[--qn];                        /* 移出隔离区，避免刷屏 */
            i--;
        }
    }
}

void *malloc(size_t n)
{
    if (!enabled) return __libc_malloc(n);
    scan_quar();
    unsigned char *p = __libc_malloc(n + HDR);
    if (!p) return 0;
    *(size_t *)p     = n;
    *(size_t *)(p+8) = MAGIC;
    __sync_fetch_and_add(&n_alloc, 1);
    return p + HDR;
}

void *calloc(size_t a, size_t b)
{
    if (!enabled) return __libc_calloc(a, b);
    size_t n = a * b;
    void *q2 = malloc(n);
    if (q2) memset(q2, 0, n);
    return q2;
}

void *realloc(void *p, size_t n)
{
    if (!enabled) return __libc_realloc(p, n);
    if (!p) return malloc(n);
    unsigned char *h = (unsigned char *)p - HDR;
    if (*(size_t *)(h + 8) != MAGIC) return __libc_realloc(p, n);
    unsigned char *np = __libc_realloc(h, n + HDR);
    if (!np) return 0;
    *(size_t *)np     = n;
    *(size_t *)(np+8) = MAGIC;
    return np + HDR;
}

void free(void *p)
{
    if (!p) return;
    if (!enabled) { __libc_free(p); return; }
    unsigned char *h = (unsigned char *)p - HDR;
    if (*(size_t *)(h + 8) != MAGIC) { __libc_free(p); return; }

    scan_quar();
    {
        size_t n = *(size_t *)h;
        if (qn < QUAR_MAX) {
            memset(h + HDR, POISON, n);            /* 毒化整块 */
            q[qn].p = h; q[qn].n = n; qn++;
            __sync_fetch_and_add(&n_free, 1);
            return;                                 /* ★ 不真释放 */
        }
        __libc_free(h);                             /* 隔离区满 ⇒ 真释放 */
    }
}

__attribute__((destructor))
static void q_report(void)
{
    char buf[128];
    int l = snprintf(buf, sizeof(buf),
        "[QUAR] alloc=%lu free=%lu uaf_hits=%lu quarantined=%d\n",
        n_alloc, n_free, n_hit, qn);
    if (l > 0) write(2, buf, l);
}
