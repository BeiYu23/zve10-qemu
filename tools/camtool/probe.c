/* 纯 syscall 探针：不链接任何 libc，用于 glibc 2.11.2 相机 */
#define SYS_open   5
#define SYS_read   3
#define SYS_write  4
#define SYS_close  6
#define SYS_getpid 20
#define O_WRONLY 1
#define O_CREAT  0100
#define O_TRUNC  01000
#define O_RDONLY 0

static long sc3(long n, long a, long b, long c) {
    register long r0 asm("r0") = a;
    register long r1 asm("r1") = b;
    register long r2 asm("r2") = c;
    register long r7 asm("r7") = n;
    asm volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static int xopen(const char *p, int fl, int md) { return (int)sc3(SYS_open, (long)p, fl, md); }
static long xread(int fd, void *b, long n) { return sc3(SYS_read, fd, (long)b, n); }
static long xwrite(int fd, const void *b, long n) { return sc3(SYS_write, fd, (long)b, n); }
static int xclose(int fd) { return (int)sc3(SYS_close, fd, 0, 0); }

static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void puts_fd(int fd, const char *s) { xwrite(fd, s, slen(s)); }

__attribute__((constructor)) static void probe_ctor(void) {
    char buf[2048];
    int fd = xopen("/log/BOOTPROBE.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    puts_fd(fd, "=== PROBE ALIVE pid=");
    /* 简单打一个 pid */
    long pid = sc3(SYS_getpid, 0, 0, 0);
    char num[16]; int i = 0;
    if (pid == 0) num[i++] = '0';
    while (pid > 0) { num[i++] = '0' + (pid % 10); pid /= 10; }
    while (i > 0) xwrite(fd, &num[--i], 1);
    puts_fd(fd, " ===\n--- /proc/self/maps ---\n");
    int m = xopen("/proc/self/maps", O_RDONLY, 0);
    if (m >= 0) {
        long n;
        while ((n = xread(m, buf, sizeof(buf))) > 0) xwrite(fd, buf, n);
        xclose(m);
    }
    puts_fd(fd, "\n=== END ===\n");
    xclose(fd);
}
