/* zvegisk_probe.c — Phase 0：零风险符号探测
 *
 * 目标：只回答一个问题 ——
 *   我们 preload 进 im.elf 的 .so，能不能拿到 libObj.so 的 GRM 绘图符号？
 *
 * ★ 本载荷【不调用任何 GRM 函数】，只取函数地址并记录。
 *   ⇒ 即使参数结构完全猜错，也不会崩（因为没有调用）。
 *   ⇒ 唯一可能的失败：符号无法解析 ⇒ 我们的 .so 加载失败 ⇒
 *      ld.so 打印警告后 im.elf 继续正常启动（非致命）。
 *
 * 输出：/log/zvegisk_probe.log（纯 syscall 写，不依赖 libc I/O）
 *
 * 编译（见 build.sh）：
 *   arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -nostdlib -O2 \
 *       -Wl,-soname,zvegisk_probe.so -o zvegisk_probe.so zvegisk_probe.c
 */

/* ---- 直接 syscall（只用于写日志，不依赖 libc 符号版本） ---- */
#define SYS_exit  1
#define SYS_fork  2
#define SYS_write 4
#define SYS_open  5
#define SYS_close 6
#define SYS_mkdir 39

static inline int s1(int n, int a) {
    register int r7 __asm__("r7") = n; register int r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc"); return r0;
}
static inline int s2(int n, int a, int b) {
    register int r7 __asm__("r7") = n; register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory", "cc"); return r0;
}
static inline int s3(int n, int a, int b, int c) {
    register int r7 __asm__("r7") = n; register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b; register int r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory", "cc"); return r0;
}

/* ---- ★ 关键的 extern 声明 ----
 * 这些符号在 libObj.so 里，im.elf 已经加载了 libObj
 * ⇒ 我们 dlopen 时，动态链接器会在全局符号表里解析它们。
 * ★ 这里只【取地址】，不调用。
 */
extern void *GRM_screenGetOnBitmap(void);
extern int   GRM_gpermFillrect(void *ctx, int a, void *rect, int b, int c);
extern int   GRM_gpermRectblit(void *a, int b, void *c, void *d);
extern int   GRM_bitmapCreate(void *out, int w, int h);
extern int   GRM_bitmapWriteLockBits(void *bmp, void *area);
extern int   GRM_bitmapWriteUnlockBits(void *bmp);
extern int   GRM_bitmapGetPhysicalAddress(void *bmp);

/* ---- 极简日志 ---- */
static char LOG[1024];
static int  LP;
static void L(const char *s) { while (*s) LOG[LP++] = *s++; }
static void LH(unsigned v) {           /* 16 进制 */
    static const char hx[] = "0123456789abcdef";
    L("0x");
    for (int sh = 28; sh >= 0; sh -= 4) LOG[LP++] = hx[(v >> sh) & 0xF];
}
static void LD(unsigned v) {           /* 10 进制 */
    char t[12]; int n = 0;
    if (!v) { LOG[LP++] = '0'; return; }
    while (v && n < 11) { t[n++] = '0' + (v % 10); v /= 10; }
    while (n) LOG[LP++] = t[--n];
}
static void LK(const char *k, unsigned v) { L(k); LH(v); L("\r\n"); }

static void dump(void)
{
    LP = 0;
    L("ZVEGISK_PROBE_START\r\n");
    L("pid="); LD((unsigned)0); L("\r\n");        /* 占位，避免依赖 getpid */

    /* ★ 只取地址，绝不调用 —— 这就是零风险的证明 */
    LK("GRM_screenGetOnBitmap      = ", (unsigned)(unsigned long)&GRM_screenGetOnBitmap);
    LK("GRM_gpermFillrect          = ", (unsigned)(unsigned long)&GRM_gpermFillrect);
    LK("GRM_gpermRectblit          = ", (unsigned)(unsigned long)&GRM_gpermRectblit);
    LK("GRM_bitmapCreate           = ", (unsigned)(unsigned long)&GRM_bitmapCreate);
    LK("GRM_bitmapWriteLockBits    = ", (unsigned)(unsigned long)&GRM_bitmapWriteLockBits);
    LK("GRM_bitmapWriteUnlockBits  = ", (unsigned)(unsigned long)&GRM_bitmapWriteUnlockBits);
    LK("GRM_bitmapGetPhysicalAddr  = ", (unsigned)(unsigned long)&GRM_bitmapGetPhysicalAddress);

    L("PROBE_DONE\r\n");

    /* 写两份：/log 便于事后取；/tmp/jiritsu 便于 agent/PULL */
    {
        int fd = s3(SYS_open, (int)"/log/zvegisk_probe.log", 0x241, 0644);
        if (fd >= 0) { int o = 0; while (o < LP) { int r = s3(SYS_write, fd, (int)(LOG + o), LP - o); if (r <= 0) break; o += r; } s1(SYS_close, fd); }
    }
    {
        int fd = s3(SYS_open, (int)"/tmp/jiritsu/zvegisk_probe.txt", 0x241, 0644);
        if (fd >= 0) { int o = 0; while (o < LP) { int r = s3(SYS_write, fd, (int)(LOG + o), LP - o); if (r <= 0) break; o += r; } s1(SYS_close, fd); }
    }
}

/* ★ 构造函数：fork 后父进程立即返回，绝不阻塞 im.elf */
__attribute__((constructor)) static void init(void)
{
    int pid = s1(SYS_fork, 0);
    if (pid == 0) {
        dump();
        s1(SYS_exit, 0);
        for (;;) { }
    }
    /* 父进程（im.elf）立刻继续 */
}
