/* payload_ping.c — 最小验证：构造函数里能否写入 /log
 * 不 fork、不 mount，只写一行
 */
#define SYS_exit   1
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6

static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}

static int wr(const char *path, const char *s, int n)
{
    int fd = sy3(SYS_open, (int)path, 0x241, 0644);
    if (fd < 0) return fd;
    int o = 0;
    while (o < n) { int r = sy3(SYS_write, fd, (int)(s+o), n-o); if (r <= 0) break; o += r; }
    sy1(SYS_close, fd);
    return o;
}

__attribute__((constructor)) static void entry(void)
{
    /* ① 先写 /log */
    int a = wr("/log/zvping.log", "PING_FROM_PAYLOAD /log OK\n", 29);
    /* ② 再写 /tmp（vfat，另一分区） */
    int b = wr("/tmp/zvping.log", "PING_FROM_PAYLOAD /tmp OK\n", 29);
    /* ③ 再写 /setting */
    int c = wr("/setting/zvping.log", "PING_FROM_PAYLOAD /setting OK\n", 34);
    /* ④ 把三个结果写进 /log 的第二行 */
    char r[64]; int L = 0;
    r[L++]='r'; r[L++]='='; 
    r[L++] = (a>=0)?'L':'-'; r[L++]=(b>=0)?'T':'-'; r[L++]=(c>=0)?'S':'-'; r[L++]='\n';
    wr("/log/zvping.log", r, L);
}
