/* beacon.so —— 极简信标：构造函数只做裸 syscall，不依赖 libc
 * 目的：证伪/证实「im.elf 是否被 exec 起来了」
 * 输出写到 /tmp（最早挂载）和 /log（持久）两处，含 pid + argv
 * 绝不 hook 任何函数、绝不 exit、绝不影响宿主
 */
#define SYS_exit_group 248
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_read        3
#define SYS_getpid     20
#define SYS_mkdir      39

static long sy(long n, long a, long b, long c){
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static int slen(const char *s){ int n=0; if(!s) return 0; while(s[n]) n++; return n; }
static void w(int fd, const char *s, int n){ if(fd>=0 && n>0) sy(SYS_write, fd, (long)s, n); }
static void ws(int fd, const char *s){ w(fd, s, slen(s)); }
/* 不用除法：反复相减，避免 __aeabi_idiv / __aeabi_idivmod */
static void wd(int fd, long v){
    static const long pw[] = {1000000000L,100000000L,10000000L,1000000L,100000L,10000L,1000L,100L,10L,1L};
    char b[16]; int n=0, started=0;
    if(v < 0){ ws(fd,"-"); v = -v; }
    if(v == 0){ ws(fd,"0"); return; }
    for(int i=0;i<10;i++){
        int d=0;
        while(v >= pw[i]){ v -= pw[i]; d++; if(d>9) break; }
        if(d || started){ b[n++] = '0'+d; started=1; }
    }
    w(fd, b, n);
}

static void beacon_one(const char *path){
    /* O_WRONLY|O_CREAT|O_APPEND = 1|64|1024 = 1089, mode 0666 */
    long fd = sy(SYS_open, (long)path, 1089, 0666);
    if(fd < 0) return;
    ws(fd, "BEACON pid=");
    wd(fd, sy(SYS_getpid,0,0,0));
    ws(fd, " argv=");
    /* 读 /proc/self/cmdline，把 \0 换成空格 */
    long cf = sy(SYS_open, (long)"/proc/self/cmdline", 0, 0);
    if(cf >= 0){
        char buf[512]; long n = sy(SYS_read, cf, (long)buf, sizeof buf - 1);
        if(n > 0){
            for(long i=0;i<n;i++) if(buf[i]==0) buf[i]=' ';
            w(fd, buf, (int)n);
        }
        sy(SYS_close, cf, 0, 0);
    }
    ws(fd, "\n");
    sy(SYS_close, fd, 0, 0);
}

__attribute__((constructor)) static void beacon(void){
    beacon_one("/tmp/.beacon.txt");   /* /tmp 最早挂载 */
    beacon_one("/log/.beacon.txt");   /* /log 持久，重启后仍可读 */
    /* 顺便留一个时间戳文件，确认构造函数执行次数 */
    long fd = sy(SYS_open, (long)"/log/.beacon.count", 1089, 0666);
    if(fd >= 0){ ws(fd, "x"); sy(SYS_close, fd, 0, 0); }
}
