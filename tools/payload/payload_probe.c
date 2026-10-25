/* payload_probe.c — Stage 1 验证载荷（最小、最保守）
 *
 * 目标：证明三件事
 *   ① 代码真的以维护进程权限跑了
 *   ② 能不能 mount（CAP_SYS_ADMIN）
 *   ③ 相机当前挂载了什么（有没有已挂载的 SD 卡）
 *
 * 设计要点：
 *   · 纯直接 syscall，不依赖任何 libc 符号 ⇒ 不可能有 glibc 版本不匹配
 *   · 构造函数里 fork，父进程立即返回，不阻塞维护进程
 *   · 子进程做完全部工作后 _exit
 *
 * 编译（见 build.sh）：
 *   arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -nostdlib -O2 \
 *       -Wl,-soname,libJit.so -o libJit.so payload_probe.c
 */

#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_execve   11
#define SYS_mount    21
#define SYS_umount2  52
#define SYS_openat  322

/* ---- 直接 syscall 封装（ARM EABI: r7 = 号, r0..r5 = 参数） ---- */
static inline int sys0(int n) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0");
    __asm__ volatile("svc #0" : "=r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}
static inline int sys1(int n, int a) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}
static inline int sys2(int n, int a, int b) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory", "cc");
    return r0;
}
static inline int sys3(int n, int a, int b, int c) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory", "cc");
    return r0;
}
static inline int sys4(int n, int a, int b, int c, int d) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r3 __asm__("r3") = d;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory", "cc");
    return r0;
}
static inline int sys5(int n, int a, int b, int c, int d, int e) {
    register int r7 __asm__("r7") = n;
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r3 __asm__("r3") = d;
    register int r4 __asm__("r4") = e;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "memory", "cc");
    return r0;
}

/* ---- 极简字符串工具 ---- */
static void sapp(char *d, int *p, const char *s) { while (*s) d[(*p)++] = *s++; }
static void sappd(char *d, int *p, int v) {
    char tmp[12]; int n = 0;
    if (v < 0) { d[(*p)++] = '-'; v = -v; }
    if (!v) { d[(*p)++] = '0'; return; }
    while (v && n < 11) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n) d[(*p)++] = tmp[--n];
}

static int open_w(const char *path) { return sys3(SYS_open, (int)path, 0x241 /*O_WRONLY|O_CREAT|O_TRUNC*/, 0644); }
static void write_all(int fd, const char *s, int n) { int off = 0; while (off < n) { int r = sys3(SYS_write, fd, (int)(s + off), n - off); if (r <= 0) break; off += r; } }

/* ---- 主工作：全部结果写进一个文件 ---- */
static int mkdir_p(const char *path);

static void work(void)
{
    char buf[2048];
    int p = 0;

    sapp(buf, &p, "PROBE_OK\r\n");

    /* ② 试 mount：把 SD 卡第一分区挂到 /tmp/sd */
    sapp(buf, &p, "MKDIR_RC=");
    sappd(buf, &p, mkdir_p("/tmp/sd"));
    sapp(buf, &p, "\r\n");

    int mrc = sys5(SYS_mount, (int)"/dev/mmcaa", (int)"/tmp/sd",
                   (int)"vfat", 0, 0);
    sapp(buf, &p, "MOUNT_VFAT_MMCAA_RC=");
    sappd(buf, &p, mrc);
    sapp(buf, &p, " ERRNO=");
    sappd(buf, &p, -mrc);
    sapp(buf, &p, "\r\n");

    /* 也试整卡 */
    int mrc2 = sys5(SYS_mount, (int)"/dev/mmca", (int)"/tmp/sd",
                    (int)"vfat", 0, 0);
    sapp(buf, &p, "MOUNT_VFAT_MMCA_RC=");
    sappd(buf, &p, mrc2);
    sapp(buf, &p, "\r\n");

    /* ③ dump /proc/mounts（看有没有已挂载的 SD 卡可用） */
    int fd = sys3(SYS_open, (int)"/proc/mounts", 0, 0);
    sapp(buf, &p, "MOUNTS_FD=");
    sappd(buf, &p, fd);
    sapp(buf, &p, "\r\n");
    if (fd >= 0) {
        char mb[1200]; int n = sys3(SYS_read, fd, (int)mb, (int)sizeof(mb) - 1);
        sys1(SYS_close, fd);
        if (n > 0) {
            mb[n] = 0;
            sapp(buf, &p, "--- MOUNTS BEGIN ---\r\n");
            for (int i = 0; i < n && p < (int)sizeof(buf) - 4; i++) {
                if (mb[i] == '\n') { buf[p++] = '\r'; buf[p++] = '\n'; }
                else buf[p++] = mb[i];
            }
            sapp(buf, &p, "\r\n--- MOUNTS END ---\r\n");
        }
    }

    sapp(buf, &p, "PROBE_DONE\r\n");

    int ofd = open_w("/tmp/jiritsu/probe.txt");
    if (ofd >= 0) {
        write_all(ofd, buf, p);
        sys1(SYS_close, ofd);
    }
}

/* mkdir 用的极简实现（路径已存在时返回 -EEXIST，视为成功） */
static int mkdir_p(const char *path)
{
    int r = sys2(39 /*SYS_mkdir*/, (int)path, 0755);
    return (r == 0 || r == -17 /*EEXIST*/) ? 0 : r;
}

__attribute__((constructor)) static void init(void)
{
    int pid = sys0(SYS_fork);
    if (pid == 0) {
        work();
        sys1(SYS_exit, 0);
        for (;;) { }
    }
    /* 父进程立即返回 */
}
