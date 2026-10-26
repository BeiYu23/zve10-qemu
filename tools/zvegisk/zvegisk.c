/* zvegisk.c — ZV-E10 开机模块加载 + 崩溃自动回退
 *
 * ★★ 安全设计（最关键）★★
 *   构造函数【第一件事就是 fork】，父进程立刻返回。
 *   ⇒ 即使子进程崩了、卡了、出错，im.elf 的 main() 照常执行，相机正常开机。
 *
 * ★ 全部用直接 syscall，不依赖任何 libc 符号（避免 glibc 2.11.2 版本问题）
 *
 * 流程（全部在子进程）：
 *   ① 读 /proc/partitions 找 SD 卡分区
 *   ② tmpfs + mknod 建设备节点
 *   ③ mount -t vfat（不带选项，实测可用）
 *   ④ 读 /zvegisk/boot_mode
 *      · safemode 存在 → 什么都不做
 *      · "new" → 遍历 mods/<name>/system/ 做 mount --bind
 *      · "old" → 什么都不做
 *   ⑤ boot_count++ ；超过阈值 → 强制回 old + 建 safemode
 *   ⑥ fork watchdog：等 90 秒查 UI 是否起来
 *      · 起来 → boot_count=0，记 last_good
 *      · 没起来 → umount 全部 bind + reboot -f
 *
 * Phase 1 可以先只做 ①-③ + ⑤ + ⑥（不 bind），验证机制。
 */

#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_unlink   10
#define SYS_mount    21
#define SYS_umount2  52
#define SYS_mkdir    39
#define SYS_mknod    14
#define SYS_getdents64 217
#define SYS_reboot   88
#define SYS_nanosleep 162
#define SYS_setsid    66
#define SYS_execve    11
#define SYS_wait4    114
#define SYS_kill     37
#define SYS_getpid   20
#define SYS_lseek    19
#define SYS_fsync   118
#define SYS_sync     36
#define SYS_ioctl    54
#define LOOP_SET_FD  0x4C00
#define MS_RDONLY    1
#define PATHMAX     192
#define MS_BIND     4096      /* 0x1000 */
#define MNT_DETACH  2

/* ---- 直接 syscall ---- */
static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

/* ---- 字符串工具 ---- */
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static int scmp(const char*a,const char*b){while(*a&&*b){if(*a!=*b)return 0;a++;b++;}return *a==*b;}
static int shas(const char*s,const char*sub){int i,j;for(i=0;s[i];i++){for(j=0;sub[j]&&s[i+j]==sub[j];j++);if(!sub[j])return 1;}return 0;}
static void scpy(char*d,const char*s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]=0;}
static void scat(char*d,const char*s,int max){int l=slen(d),i=0;while(s[i]&&l+i<max-1){d[l+i]=s[i];i++;}d[l+i]=0;}

/* ---- 日志 ---- */
/* ★ 日志降级链：/log 优先，失败退 /tmp、再退 /setting
 *   绝不能因为日志写不了而影响启动。 */
static const char *LOG_PATHS[] = {
    "/log/zvegisk.log",
    "/tmp/zvegisk.log",
    "/setting/zvegisk.log",
    0
};
static char LOGPATH[64];      /* 首次成功写入后固定下来 */
static char LB[4096]; static int LP;
static void lg(const char*s){while(*s&&LP<4094)LB[LP++]=*s++;}
static void lc(char c){if(LP<4094)LB[LP++]=c;}
static void lgd(int v)
{
    /* 纯减法实现十进制输出，避免 __aeabi_idiv */
    static const int pw[10] = {1000000000,100000000,10000000,1000000,
                               100000,10000,1000,100,10,1};
    if (v < 0) { lg("-"); v = -v; }
    if (!v) { lc('0'); return; }
    int started = 0;
    for (int i = 0; i < 10; i++) {
        int d = 0;
        while (v >= pw[i]) { v -= pw[i]; d++; }
        if (d || started) { lc((char)('0' + d)); started = 1; }
    }
}
static void flushlog(void){
    if (LP <= 0) return;
    int fd = -1;
    if (LOGPATH[0]) {
        fd = sy3(SYS_open, (int)LOGPATH, 0x441 /*O_WRONLY|O_CREAT|O_APPEND*/, 0644);
    } else {
        for (int i = 0; LOG_PATHS[i]; i++) {
            fd = sy3(SYS_open, (int)LOG_PATHS[i], 0x441 /*O_APPEND*/, 0644);
            if (fd >= 0) { scpy(LOGPATH, LOG_PATHS[i], sizeof LOGPATH); break; }
        }
    }
    if (fd < 0) { LP = 0; return; }        /* 全失败：静默丢弃，绝不阻塞 */
    int o = 0;
    while (o < LP) { int r = sy3(SYS_write, fd, (int)(LB+o), LP-o); if (r <= 0) break; o += r; }
    sy1(SYS_close, fd);
    LP = 0;
}

/* ---- 文件读写小工具 ---- */
static int readfile(const char*path,char*buf,int cap){
    int fd=sy3(SYS_open,(int)path,0,0);
    if(fd<0)return -1;
    int n=sy3(SYS_read,fd,(int)buf,cap-1);
    sy1(SYS_close,fd);
    if(n<0)return -1;
    buf[n]=0;
    return n;
}
static int writefile(const char*path,const char*data,int len){
    int fd=sy3(SYS_open,(int)path,0x241,0644);
    if(fd<0)return -1;
    int o=0; while(o<len){int r=sy3(SYS_write,fd,(int)(data+o),len-o);if(r<=0)break;o+=r;}
    sy1(SYS_close,fd);
    return o;
}

static int exists(const char *path)
{
    int fd = sy3(SYS_open, (int)path, 0, 0);
    if (fd < 0) return 0;
    sy1(SYS_close, fd);
    return 1;
}

/* 扫描 /proc/partitions 找 SD 分区（mmca*），返回 1 找到 */
static int find_sd(int *maj,int *min){
    static char buf[2048];
    int n=readfile("/proc/partitions",buf,sizeof buf);
    if(n<=0)return 0;
    int found=0;
    char *p=buf;
    while(*p){
        /* 跳过行首空白，解析 "major minor blocks name" */
        while(*p==' '||*p=='\n')p++;
        if(!*p)break;
        int a=0,b=0; unsigned long k=0;
        while(*p>='0'&&*p<='9'){a=a*10+(*p-'0');p++;}
        while(*p==' ')p++;
        while(*p>='0'&&*p<='9'){b=b*10+(*p-'0');p++;}
        while(*p==' ')p++;
        while(*p>='0'&&*p<='9'){k=k*10+(*p-'0');p++;}
        while(*p==' ')p++;
        char nm[32]; int i=0;
        while(*p&&*p!='\n'&&i<31){nm[i++]=*p++;}
        nm[i]=0;
        if(shas(nm,"mmca")){
            /* 优先带 'p' 的分区（mmca10p1） */
            if(!found || shas(nm,"p")){*maj=a;*min=b;found=1;if(shas(nm,"p"))break;}
        }
        while(*p&&*p!='\n')p++;
    }
    return found;
}

/* 挂载 SD 卡到 /tmp/zsd，成功返回 0 */
static int mount_sd(void)
{
    int maj=0,min=0;
    if(!find_sd(&maj,&min)){lg("no_sd_partition\n");return -1;}
    lg("sd=");lgd(maj);lg(":");lgd(min);lg("\n");

    sy2(SYS_mkdir,(int)"/tmp/zdev",0755);
    /* ★ tmpfs 可能已被 check_loop() 挂好；重复挂会失败，但那不是错误。
     *   判据：挂上失败后只要目录可用就继续。 */
    if(sy5(SYS_mount,(int)"tmpfs",(int)"/tmp/zdev",(int)"tmpfs",0,0)){
        lg("tmpfs already? -> continue\n");
    }

    sy1(SYS_unlink,(int)"/tmp/zdev/p1");
    /* dev_t: (major<<8)|minor（小号够用） */
    unsigned dev=((unsigned)maj<<8)|(unsigned)min;
    if(sy3(SYS_mknod,(int)"/tmp/zdev/p1",(060660),(int)dev)){lg("mknod_fail\n");return -1;}

    sy2(SYS_mkdir,(int)"/tmp/zsd",0755);
    /* ★★★ 必须【只读】挂载：
     *   vfat 不支持多重读写挂载 —— 我们若读写挂上，相机自己的挂载会 EINVAL，
     *   导致无法录制/回放。只读允许多重挂载（实测过）。 */
    if(sy5(SYS_mount,(int)"/tmp/zdev/p1",(int)"/tmp/zsd",(int)"vfat",1 /*MS_RDONLY*/,0)){
        lg("mount_ro_fail\n");return -1;
    }
    lg("sd_mounted\n");
    return 0;
}

/* ============================================================
 * 模块框架
 *
 * 安全模型（四层）：
 *   ① 进程隔离：模块在 fork+exec 的载体进程里 LD_PRELOAD，
 *      模块崩了只死载体，im.elf 完全无感
 *   ② 超时收割：载体 N 秒不退 ⇒ SIGKILL，标记失败
 *   ③ 失败黑名单：失败模块写 <name>.failed，下次开机跳过
 *   ④ 全局熔断：失败数 > MAX_FAIL ⇒ 之后全部跳过
 * ============================================================ */

/* ★ 双目录架构（借鉴 KernelSU meta-overlayfs）
 *   元数据（小、要可靠、启动早期就要读）→ /setting（20MB，独立分区）
 *   内容  （大、可能几十 MB）           → /log（500MB）
 *   好处：改开关不碰 /log；/log 被清也不丢开关状态；
 *         扫描 20 个 module.prop 比扫 20 个含大文件的目录快得多。 */
#define METADIR  "/setting/zvegisk/mods"     /* module.prop / disabled / .failed */
#define CONTDIR  "/log/zvegisk/mods"         /* lib/NAME.so 与 system/ 实体 */
#define STATEDIR "/setting/zvegisk"          /* boot_count / safemode / 记录   */
#define MOUNTREC "/setting/zvegisk/mounts.active"
#define MAX_FAIL 3
#define MAX_MODS 32
#define MAX_OVL  32

static int g_ovl_cnt;          /* 本次已挂载的覆盖层数量 */

/* ★ 关键写入 + 落盘
 *   vfat 的 close() 不保证数据已写入 flash。
 *   如果系统在写完的下一秒就 panic 重启，数据会丢 ——
 *   而那正是"连续失败计数"最需要可靠的时候。
 *   ⇒ 关键文件（boot_count / safemode）必须 fsync。 */
static int writefile_sync(const char *path, const char *data, int len)
{
    int fd = sy3(SYS_open, (int)path, 0x241 /*O_WRONLY|O_CREAT*/, 0644);
    if (fd < 0) return -1;
    int o = 0;
    while (o < len) {
        int r = sy3(SYS_write, fd, (int)(data + o), len - o);
        if (r <= 0) break;
        o += r;
    }
    sy1(SYS_fsync, fd);        /* ★ 刷到 flash */
    sy1(SYS_close, fd);
    return o;
}

/* 判断路径是否存在（文件或目录） */

/* 睡 n 毫秒（用 nanosleep，精度足够） */
static void msleep(int ms)
{
    int ts[2];
    ts[0] = ms / 1000;
    ts[1] = (ms % 1000) * 1000000;
    sy2(SYS_nanosleep, (int)ts, 0);
}

/* ============================================================
 * 自检 ①：libIMDB 到底从哪里加载的
 *   ⇒ 判断 preload 三条目方案是否生效，不需要串口
 * ============================================================ */
static void check_preload(void)
{
    static char buf[32768];
    int n = readfile("/proc/self/maps", buf, sizeof buf);
    if (n <= 0) { lg("preload_chk: /proc/self/maps FAIL\n"); return; }

    char *p = buf;
    int found = 0;
    while (*p && !found) {
        char *e = p;
        while (*e && *e != '\n') e++;
        int hit = 0;
        for (char *t = p; t + 7 <= e; t++) {
            if (t[0]=='l'&&t[1]=='i'&&t[2]=='b'&&t[3]=='I'&&t[4]=='M'&&t[5]=='D'&&t[6]=='B') { hit = 1; break; }
        }
        if (hit) {
            lg("preload_chk: libIMDB -> ");
            for (char *t = p; t < e && t < p + 160; t++) lc(*t);
            lg("\n");
            found = 1;
        }
        p = e;
        if (*p == '\n') p++;
    }
    if (!found) lg("preload_chk: libIMDB NOT in maps\n");

    /* 顺便看 libObj / appFw 是否已加载（在启动早期应该是【没有】的） */
    const char *names[] = { "libObj", "appFw", 0 };
    for (int k = 0; names[k]; k++) {
        char *q = buf; int got = 0;
        while (*q && !got) {
            char *e2 = q;
            while (*e2 && *e2 != '\n') e2++;
            if (shas(q, names[k])) {
                lg("preload_chk: "); lg(names[k]); lg(" -> ");
                for (char *t = q; t < e2 && t < q + 130; t++) lc(*t);
                lg("\n");
                got = 1;
            }
            q = e2; if (*q == '\n') q++;
        }
        if (!got) { lg("preload_chk: "); lg(names[k]); lg(" not yet loaded\n"); }
    }
}

/* ============================================================
 * 自检 ②：loop 设备是否可用
 *   只在 /log/zvegisk/m2usr.img 存在时做完整测试
 * ============================================================ */
static void check_loop(void)
{
    /* /tmp 是 vfat，不能 mknod ⇒ 必须先挂 tmpfs */
    sy2(SYS_mkdir, (int)"/tmp/zdev", 0755);
    if (sy5(SYS_mount, (int)"tmpfs", (int)"/tmp/zdev", (int)"tmpfs", 0, 0) != 0)
        lg("loop_chk: tmpfs mount FAIL\n");

    sy1(SYS_unlink, (int)"/tmp/zdev/loop0");
    int r = sy3(SYS_mknod, (int)"/tmp/zdev/loop0", 060660, (7 << 8) | 0);
    if (r != 0) { lg("loop_chk: mknod FAIL (errno-ish "); lgd(r); lg(")\n"); }
    else        lg("loop_chk: mknod OK\n");

    int lfd = sy3(SYS_open, (int)"/tmp/zdev/loop0", 2 /*O_RDWR*/, 0);
    if (lfd < 0) { lg("loop_chk: open loop0 FAIL\n"); return; }
    lg("loop_chk: open loop0 OK\n");

    if (!exists("/log/zvegisk/m2usr.img")) {
        lg("loop_chk: m2usr.img absent -> basic test only\n");
        sy1(SYS_close, lfd);
        return;
    }

    int ifd = sy3(SYS_open, (int)"/log/zvegisk/m2usr.img", 0 /*O_RDONLY*/, 0);
    if (ifd < 0) { lg("loop_chk: open image FAIL\n"); sy1(SYS_close, lfd); return; }

    r = sy3(SYS_ioctl, lfd, LOOP_SET_FD, ifd);
    if (r != 0) { lg("loop_chk: LOOP_SET_FD FAIL "); lgd(r); lg("\n"); sy1(SYS_close, ifd); sy1(SYS_close, lfd); return; }
    lg("loop_chk: LOOP_SET_FD OK\n");

    sy2(SYS_mkdir, (int)"/tmp/m2usr", 0755);
    r = sy5(SYS_mount, (int)"/tmp/zdev/loop0", (int)"/tmp/m2usr", (int)"ext2", MS_RDONLY, 0);
    if (r != 0) { lg("loop_chk: mount ext2 FAIL "); lgd(r); lg("\n"); }
    else        lg("loop_chk: mount ext2 OK => loop USABLE\n");

    sy1(SYS_close, ifd);
    sy1(SYS_close, lfd);
}

/* ============================================================
 * 自检 ③：/proc/mounts 里是否出现我们的 bind
 *   ⇒ 直接证明 bind 做成了，而不是靠推断
 * ============================================================ */
static void check_mounts(void)
{
    static char buf[8192];
    int n = readfile("/proc/mounts", buf, sizeof buf);
    if (n <= 0) { lg("mounts_chk: /proc/mounts FAIL\n"); return; }
    const char *keys[] = { "/usr/lib", "/usr/share/app", "/tmp/m2usr", 0 };
    for (int k = 0; keys[k]; k++) {
        char *p = buf; int got = 0;
        while (*p && !got) {
            char *e = p;
            while (*e && *e != '\n') e++;
            if (shas(p, keys[k])) {
                lg("mounts_chk: ");
                for (char *t = p; t < e && t < p + 120; t++) lc(*t);
                lg("\n");
                got = 1;
            }
            p = e;
            if (*p == '\n') p++;
        }
        if (!got) { lg("mounts_chk: no "); lg(keys[k]); lg("\n"); }
    }
}

/* ============================================================
 * 自检 ④：延迟快照
 *   libObj / appFw 是运行时 dlopen 的，构造函数跑时还没加载
 *   ⇒ 30 秒后再拍一次 maps，看它们最终从哪加载
 * ============================================================ */
static void check_delayed(void)
{
    for (int i = 0; i < 30; i++) { int ts[2]; ts[0] = 1; ts[1] = 0; sy2(SYS_nanosleep, (int)ts, 0); }
    LP = 0;
    lg("=== delayed snapshot (+30s) ===\n");
    check_preload();       /* 复用它：会打 libIMDB / libObj / appFw 三行 */
    check_mounts();
    flushlog();
}

/* 等子进程，最多 timeout_ms；超时则 SIGKILL。返回：
 *   >=0  子进程退出码
 *   -1   超时被杀
 *   -2   wait4 出错 */
static int wait_child(int pid, int timeout_ms)
{
    int st = 0;
    int waited = 0;
    for (;;) {
        int r = sy4(SYS_wait4, pid, (int)&st, 1 /*WNOHANG*/, 0);
        if (r == pid) {
            if ((st & 0x7f) == 0) return (st >> 8) & 0xff;   /* 正常退出 */
            return 128 + (st & 0x7f);                         /* 被信号杀 */
        }
        if (r < 0 && r != -11 /*EAGAIN*/) return -2;
        if (waited >= timeout_ms) {
            sy2(SYS_kill, pid, 9);          /* SIGKILL */
            sy4(SYS_wait4, pid, (int)&st, 0, 0);
            return -1;
        }
        msleep(50);
        waited += 50;
    }
}

/* 检查某 TCP 端口是否在 LISTEN（读 /proc/net/tcp）
 * 用来验证"模块真的起来了"，而不是只看载体退出码 */
static int port_listening(int port)
{
    static char buf[8192];
    int n = readfile("/proc/net/tcp", buf, sizeof buf);
    if (n <= 0) return 0;
    char *p = buf;
    /* 跳过表头行 */
    while (*p && *p != '\n') p++;
    while (*p) {
        if (*p == '\n') p++;
        if (!*p) break;
        /* 每行: sl local_address rem_address st ...
         * local_address 形如 "0100007F:1E61"（端口小端 hex） */
        char *q = p;
        int field = 0;
        char *local = 0;
        while (*q && *q != '\n') {
            while (*q == ' ') q++;
            if (field == 1) { local = q; break; }
            while (*q && *q != ' ' && *q != '\n') q++;
            field++;
        }
        if (local) {
            char *colon = 0;
            for (char *t = local; *t && *t != ' ' && *t != '\n'; t++)
                if (*t == ':') colon = t;
            if (colon) {
                int v = 0;
                for (char *t = colon + 1; *t && *t != ' ' && *t != '\n'; t++) {
                    int d;
                    if (*t >= '0' && *t <= '9') d = *t - '0';
                    else if (*t >= 'A' && *t <= 'F') d = *t - 'A' + 10;
                    else if (*t >= 'a' && *t <= 'f') d = *t - 'a' + 10;
                    else break;
                    v = v * 16 + d;
                }
                /* /proc/net/tcp 的端口是主机字节序的十六进制串 */
                if (v == port) return 1;
            }
        }
        while (*p && *p != '\n') p++;
    }
    return 0;
}

/* 读 module.prop 里的某个整数键，如 verify_port=7777
 * 找不到返回 -1 */
static int prop_int(const char *dir, const char *key)
{
    char path[160];
    static char buf[512];
    scpy(path, dir, sizeof path);
    scat(path, "/module.prop", sizeof path);
    if (readfile(path, buf, sizeof buf) <= 0) return -1;
    int klen = slen(key);
    for (char *p = buf; *p; ) {
        /* 逐行 */
        char *e = p;
        while (*e && *e != '\n') e++;
        /* 该行是否以 key= 开头 */
        int i = 0;
        while (i < klen && p[i] == key[i]) i++;
        if (i == klen && p[i] == '=') {
            int v = 0, any = 0;
            for (char *t = p + i + 1; t < e && *t >= '0' && *t <= '9'; t++) {
                v = v * 10 + (*t - '0'); any = 1;
            }
            if (any) return v;
        }
        p = e;
        if (*p == '\n') p++;
    }
    return -1;
}

/* 从双目录读 module.prop 里的整数键 */
static int prop_int_dual(const char *name, const char *key)
{
    char p[PATHMAX];
    int v;
    scpy(p, METADIR, PATHMAX); scat(p, "/", PATHMAX); scat(p, name, PATHMAX);
    v = prop_int(p, key);
    if (v >= 0) return v;
    scpy(p, CONTDIR, PATHMAX); scat(p, "/", PATHMAX); scat(p, name, PATHMAX);
    return prop_int(p, key);
}

/* 载入一个代码模块：fork 出载体进程，LD_PRELOAD 模块 .so
 * 返回 0 成功，<0 失败 */
static int run_code_module(const char *name, const char *so, int verify_port, int timeout_ms)
{
    lg("  load "); lg(name);
    lg(" verify_port="); lgd(verify_port);
    lg("\n");

    int pid = sy0(SYS_fork);
    if (pid < 0) { lg("  fork FAIL\n"); return -1; }
    if (pid == 0) {
        /* ---- 载体进程：崩了也只死这里 ---- */
        sy0(SYS_setsid);
        static char env[256];
        scpy(env, "LD_PRELOAD=", sizeof env);
        scat(env, so, sizeof env);
        static char a0[] = "busybox";
        static char a1[] = "true";
        static char *argv[] = { a0, a1, 0 };
        static char *envp[] = { env, 0 };
        /* 先试 /bin/busybox，再试 /usr/bin/busybox */
        sy3(SYS_execve, (int)"/bin/busybox", (int)argv, (int)envp);
        sy3(SYS_execve, (int)"/usr/bin/busybox", (int)argv, (int)envp);
        sy3(SYS_execve, (int)"/bin/true", (int)argv, (int)envp);
        sy1(SYS_exit, 127);
        for (;;) {}
    }

    int rc = wait_child(pid, timeout_ms);
    if (rc < 0) {
        lg(rc == -1 ? "  TIMEOUT -> killed\n" : "  wait4 error\n");
        return -1;
    }
    lg("  carrier exit="); lgd(rc); lg("\n");
    if (rc != 0) {
        lg("  NONZERO -> fail\n");
        return -1;
    }
    /* 载体正常退出。若声明了 verify_port，确认服务真的起来了 */
    if (verify_port > 0) {
        int ok = 0;
        for (int i = 0; i < 20; i++) {          /* 最多等 2 秒 */
            if (port_listening(verify_port)) { ok = 1; break; }
            msleep(100);
        }
        if (!ok) { lg("  verify_port NOT listening -> fail\n"); return -1; }
        lg("  verify_port OK\n");
    }
    return 0;
}



/* ------------------------------------------------------------
 * 双目录模块枚举
 *   元数据：/setting/zvegisk/mods/<name>/module.prop|disabled|<name>.failed
 *   内容  ：/log/zvegisk/mods/<name>/lib/<name>.so | system/...
 *
 * 扫描顺序：先扫 METADIR（拿开关状态），再扫 CONTDIR（补只有内容的模块）。
 * 同名以 METADIR 的元数据为准。
 * ------------------------------------------------------------ */

/* 已处理过的模块名（去重） */
static char g_done[MAX_MODS][48];
static int  g_done_n;

static int already_done(const char *nm)
{
    for (int i = 0; i < g_done_n; i++)
        if (scmp(g_done[i], nm)) return 1;
    return 0;
}
static void set_done(const char *nm, int idx)
{
    if (idx < 0 || idx >= MAX_MODS) return;
    int i = 0;
    while (nm[i] && i < 47) { g_done[idx][i] = nm[i]; i++; }
    g_done[idx][i] = 0;
}

/* 读某个模块的元数据文件路径（METADIR 优先，回退 CONTDIR） */
static void meta_path(char *out, const char *name, const char *leaf, int meta_first)
{
    scpy(out, meta_first ? METADIR : CONTDIR, PATHMAX);
    scat(out, "/", PATHMAX);
    scat(out, name, PATHMAX);
    scat(out, "/", PATHMAX);
    scat(out, leaf, PATHMAX);
}

/* 前置声明 */
static int apply_overlay_tree(const char *modroot, const char *rel, int *applied);

/* 处理单个模块 */
static int process_module(const char *nm, int *fails, int phase)
{
    char p[PATHMAX];

    /* disabled 或 failed ⇒ 跳过 */
    meta_path(p, nm, "disabled", 1);
    if (exists(p)) { lg("  disabled\n"); return 0; }
    meta_path(p, nm, "disabled", 0);
    if (exists(p)) { lg("  disabled\n"); return 0; }

    char failed[PATHMAX];
    scpy(failed, STATEDIR, PATHMAX); scat(failed, "/", PATHMAX);
    scat(failed, nm, PATHMAX); scat(failed, ".failed", PATHMAX);
    if (exists(failed)) { lg("  blacklisted\n"); return 0; }

    /* 代码模块：优先 /log 的 lib/<name>.so，回退 /setting */
    char so[PATHMAX];
    scpy(so, CONTDIR, PATHMAX); scat(so, "/", PATHMAX); scat(so, nm, PATHMAX);
    scat(so, "/lib/", PATHMAX); scat(so, nm, PATHMAX); scat(so, ".so", PATHMAX);
    if (!exists(so)) {
        scpy(so, METADIR, PATHMAX); scat(so, "/", PATHMAX); scat(so, nm, PATHMAX);
        scat(so, "/lib/", PATHMAX); scat(so, nm, PATHMAX); scat(so, ".so", PATHMAX);
    }
    if (phase == 0 && exists(so)) {
        int vp = prop_int_dual(nm, "verify_port");
        if (run_code_module(nm, so, vp, 8000) < 0) {
            lg("  FAIL -> blacklist\n");
            writefile(failed, "1\n", 2);
            (*fails)++;
        } else lg("  OK\n");
        return 0;
    }

    /* 资源模块：看 /log 有没有 system/ */
    char sysd[PATHMAX];
    scpy(sysd, CONTDIR, PATHMAX); scat(sysd, "/", PATHMAX); scat(sysd, nm, PATHMAX);
    scat(sysd, "/system", PATHMAX);
    if (phase == 1 && exists(sysd)) {
        static int applied;
        applied = 0;
        lg("  overlay:\n");
        apply_overlay_tree(sysd, "", &applied);
        lg("  applied "); lgd(applied); lg(" file(s)\n");
        return 0;
    }

    if (phase == 0) lg("  (no lib/, not a code module)\n");
    else            lg("  (no system/, not an overlay module)\n");
    return 0;
}

/* 扫一个目录，对每个子目录调 process_module */
static void scan_dir(const char *dir, int *fails, int *count, int phase)
{
    int dfd = sy4(SYS_open, (int)dir, 0x10000 /*O_DIRECTORY*/, 0, 0);
    if (dfd < 0) { lg("  (no "); lg(dir); lg(")\n"); return; }

    static char dbuf[4096];
    int n;
    while ((n = sy3(217 /*getdents64*/, dfd, (int)dbuf, sizeof dbuf)) > 0) {
        for (int off = 0; off < n; ) {
            unsigned short reclen = *(unsigned short *)(dbuf + off + 16);
            char *nm = dbuf + off + 19;
            if (reclen == 0) break;
            if (nm[0] != '.') {
                if (*fails >= MAX_FAIL) { lg("  BREAKER open, skip rest\n"); off += reclen; continue; }
                if (already_done(nm)) { off += reclen; continue; }
                if (*count >= MAX_MODS) { lg("  too many, stop\n"); break; }
                set_done(nm, g_done_n++);
                (*count)++;
                lg("mod: "); lg(nm); lg("\n");
                process_module(nm, fails, phase);
            }
            off += reclen;
        }
        if (*count >= MAX_MODS) break;
    }
    sy1(SYS_close, dfd);
}

/* 加载全部模块 */
static void load_modules(int phase)
{
    lg("--- modules ---\n");
    sy2(SYS_mkdir, (int)"/setting/zvegisk", 0755);
    sy2(SYS_mkdir, (int)METADIR, 0755);
    sy2(SYS_mkdir, (int)CONTDIR, 0755);
    sy2(SYS_mkdir, (int)"/log/zvegisk", 0755);

    g_done_n = 0;
    g_ovl_cnt = 0;

    /* ---- ① agent 永远第一个（只在 phase 0）---- */
    if (phase == 0 && exists("/log/zvegisk/agent.so")) {
        int en = 0;
        static char b[16];
        if (readfile("/log/zvegisk/agent_enable", b, sizeof b) > 0 && shas(b, "true")) en = 1;
        if (!en && readfile("/setting/zvegisk/agent_enable", b, sizeof b) > 0 && shas(b, "true")) en = 1;
        if (en) {
            char af[PATHMAX];
            scpy(af, STATEDIR, PATHMAX); scat(af, "/agent.failed", PATHMAX);
            if (exists(af)) lg("  agent SKIPPED (blacklisted)\n");
            else if (run_code_module("agent", "/log/zvegisk/agent.so", 7777, 8000) < 0) {
                lg("  agent FAIL -> blacklist\n");
                writefile(af, "1\n", 2);
            } else lg("  agent OK\n");
        } else lg("  agent disabled\n");
    } else if (phase == 0) lg("  no agent.so\n");

    /* ---- ② 双目录扫描 ---- */
    int fails = 0, count = 0;
    lg(phase ? "[meta/ovl] " : "[meta/code] "); scan_dir(METADIR, &fails, &count, phase);
    lg(phase ? "[cont/ovl] " : "[cont/code] "); scan_dir(CONTDIR, &fails, &count, phase);

    lg("--- phase "); lgd(phase); lg(" done, fails="); lgd(fails); lg(", seen="); lgd(count); lg(" ---\n");
}

/* ============================================================
 * 覆盖层挂载记录 + 精确回滚（替代 KernelSU 的 source=KSU 标识）
 *
 * ★ 为什么不用 source 标识：
 *   内核 3.0.27 没有新 mount API（fsopen/fsconfig 是 5.2+），
 *   且 mount(2) 带 MS_BIND 时 data 参数被内核忽略 ⇒ 无法打标签。
 * ⇒ 改为把 (src,dst) 写进记录文件，回滚时只卸记录里的点，
 *   不误伤系统自己的挂载。
 * ============================================================ */

static int bind_mount(const char *src, const char *dst)
{
    return sy5(SYS_mount, (int)src, (int)dst, 0, MS_BIND, 0);
}

/* 记录一次挂载 */
static void rec_mount(const char *src, const char *dst)
{
    if (g_ovl_cnt >= MAX_OVL) return;
    int fd = sy3(SYS_open, (int)MOUNTREC, 0x441 /*O_APPEND*/, 0644);
    if (fd < 0) return;
    sy3(SYS_lseek, fd, 0, 2 /*SEEK_END*/);
    char line[PATHMAX * 2];
    int n = 0;
    for (const char *q = src; *q && n < PATHMAX - 2; q++) line[n++] = *q;
    line[n++] = '\t';
    for (const char *q = dst; *q && n < PATHMAX * 2 - 2; q++) line[n++] = *q;
    line[n++] = '\n';
    sy3(SYS_write, fd, (int)line, n);
    sy1(SYS_close, fd);
    g_ovl_cnt++;
}

/* 按记录回滚：倒序卸载，只卸我们挂过的点 */
static void rollback_mounts(void)
{
    static char buf[PATHMAX * 8];
    int n = readfile(MOUNTREC, buf, sizeof buf);
    if (n <= 0) { lg("rollback: no record\n"); return; }
    lg("rollback: "); lgd(g_ovl_cnt); lg(" mount(s)\n");

    static char *lines[MAX_OVL];
    int cnt = 0;
    char *q = buf;
    while (*q && cnt < MAX_OVL) {
        lines[cnt++] = q;
        while (*q && *q != '\n') q++;
        if (*q == '\n') { *q = 0; q++; }
    }
    for (int i = cnt - 1; i >= 0; i--) {
        char *tab = 0;
        for (char *t = lines[i]; *t; t++) if (*t == '\t') tab = t;
        if (!tab) continue;
        *tab = 0;
        const char *dst = tab + 1;
        sy2(SYS_umount2, (int)dst, MNT_DETACH);
        lg("  umount "); lg(dst); lg("\n");
    }
    sy1(SYS_unlink, (int)MOUNTREC);
}

/* ------------------------------------------------------------
 * 覆盖层应用（Phase 2：单文件 bind）
 *
 * 只处理【目标已存在】的文件 —— 因为 mount --bind 不能新建文件。
 * 目标不存在的情况（比如新增 appFw.so）需要目录级覆盖（符号链接农场），
 * 那是 Phase 3。
 *
 * ★ 单文件 bind 足够覆盖：声音、图片、字体、已有 .uxc/.xdb。
 * ------------------------------------------------------------ */

#define OVL_MAXDEPTH 6

static int apply_overlay_tree(const char *modroot, const char *rel, int *applied)
{
    /* ★ 深度限制：防止异常深的目录树把栈吃光（相机只有 128MB，
     *   不能像手机那样随便递归）。正常 /usr/share 最多 3~4 层。 */
    int depth = 0;
    for (const char *t = rel; *t; t++) if (*t == '/') depth++;
    if (depth > OVL_MAXDEPTH) { lg("    SKIP(too deep)\n"); return -1; }

    char dir[PATHMAX];
    scpy(dir, modroot, PATHMAX);
    if (rel[0]) { scat(dir, "/", PATHMAX); scat(dir, rel, PATHMAX); }

    int dfd = sy4(SYS_open, (int)dir, 0x10000 /*O_DIRECTORY*/, 0, 0);
    if (dfd < 0) return -1;

    /* ★ 必须是每层独立的栈缓冲，不能用 static ——
     *   否则递归时子层覆盖父层的目录内容，父层继续读就是垃圾。 */
    char dbuf[1024];
    int n;
    while ((n = sy3(217 /*getdents64*/, dfd, (int)dbuf, sizeof dbuf)) > 0) {
        for (int off = 0; off < n; ) {
            unsigned short reclen = *(unsigned short *)(dbuf + off + 16);
            unsigned char  dtype  = *(unsigned char  *)(dbuf + off + 18);
            char *nm = dbuf + off + 19;
            if (reclen == 0) break;
            if (nm[0] != '.') {
                char child[PATHMAX];
                scpy(child, rel, PATHMAX);
                if (child[0]) scat(child, "/", PATHMAX);
                scat(child, nm, PATHMAX);

                if (dtype == 4 /*DT_DIR*/) {
                    apply_overlay_tree(modroot, child, applied);
                } else {
                    char src[PATHMAX], dst[PATHMAX];
                    scpy(src, modroot, PATHMAX); scat(src, "/", PATHMAX); scat(src, child, PATHMAX);
                    scpy(dst, "/usr/", PATHMAX);  scat(dst, child, PATHMAX);
                    if (!exists(dst)) {
                        lg("    SKIP(no target) "); lg(dst); lg("\n");
                    } else if (bind_mount(src, dst) == 0) {
                        rec_mount(src, dst);
                        (*applied)++;
                        lg("    bind "); lg(dst); lg("\n");
                    } else {
                        lg("    BIND FAIL "); lg(dst); lg("\n");
                    }
                }
            }
            off += reclen;
        }
    }
    sy1(SYS_close, dfd);
    return 0;
}

/* ============================================================
 * ★★★ 快速循环检测（针对"1 秒重启一次"这种场景）
 *
 * 思路：不能只靠 boot_count 数到 4 —— 万一系统在写完之前就 panic，
 * 计数永远加不上去。
 *
 * 改为检测【上次启动有没有走完】：
 *   开工时写 "RUNNING"，收工时写 "DONE"
 *   ⇒ 下次开机如果读到 "RUNNING"，说明上次死在半路
 *   ⇒ 连续 2 次死在半路 ⇒ 立刻删 preload 自救
 *
 * 这个信号不依赖计数累加，只要我们的代码能跑起来一次就能测出来。
 * ============================================================ */

#define PHASE_FILE "/setting/zvegisk/phase"
#define RAPID_FILE "/setting/zvegisk/rapid_fail"

static void phase_set(const char *v)
{
    writefile_sync(PHASE_FILE, v, slen(v));
}

/* 读一个小整数文件 */
static int read_small_int(const char *path)
{
    static char b[16];
    int v = 0, any = 0;
    if (readfile(path, b, sizeof b) <= 0) return 0;
    for (char *q = b; *q >= '0' && *q <= '9'; q++) { v = v * 10 + (*q - '0'); any = 1; }
    return any ? v : 0;
}

static void write_small_int(const char *path, int v)
{
    char t[8]; int m = 0, l = 0;
    if (v <= 0) t[m++] = '0';
    while (v > 0) { t[m++] = (char)('0' + (v % 10)); v /= 10; }
    char o[8];
    while (m > 0) o[l++] = t[--m];
    writefile_sync(path, o, l);
}

/* 立刻把自己从 preload 摘掉 */
static void self_disable(const char *why)
{
    lg("SELF-DISABLE ("); lg(why); lg(")\n");
    writefile_sync("/setting/zvegisk/safemode", "1\n", 2);
    flushlog();
    sy1(SYS_unlink, (int)"/setting/mode/preload");
    sy0(SYS_sync);
    lg("preload removed -> stock boot next time\n");
    flushlog();
}

/* 立刻卸载 SD 卡，把冲突窗口压到毫秒级 */
static void unmount_sd(void)
{
    sy2(SYS_umount2,(int)"/tmp/zsd",0);
    sy2(SYS_umount2,(int)"/tmp/zdev",0);
    lg("sd_unmounted\n");
}

/* 检查 UI 是否起来了：扫 /proc/PID/maps 找 viewUnified 或 appFw */
static int ui_running(void)
{
    for(int pid=1;pid<4000;pid++){
        char p[64]; scpy(p,"/proc/",64); 
        /* 数字转字符串 */
        { /* 用减法把 pid 转成十进制，避免除法 */
          char t[10]; int n=0,v=pid;
          static const int pw[5]={10000,1000,100,10,1};
          int started=0;
          if(!v){t[n++]='0';}
          else for(int k=0;k<5;k++){int d=0;while(v>=pw[k]){v-=pw[k];d++;}if(d||started){t[n++]=(char)('0'+d);started=1;}}
          for(int i=0;i<n;i++){int l=slen(p);p[l]=t[i];p[l+1]=0;} }
        scat(p,"/maps",64);
        static char mb[8192];
        int n=readfile(p,mb,sizeof mb);
        if(n<=0)continue;
        if(shas(mb,"viewUnified")||shas(mb,"appFw")) return 1;
    }
    return 0;
}

/* watchdog：等 90 秒，确认 UI 起来；失败则重启 */
static void watchdog(void)
{
    /* 简单睡眠：90 次 1 秒 */
    for(int i=0;i<90;i++){
        int ts[2]; ts[0]=1; ts[1]=0;
        sy2(SYS_nanosleep,(int)ts,0);
    }
    LP=0;
    if (!exists("/setting/zvegisk/armed")) {
        lg("watchdog: armed gone -> abort\n");
        flushlog();
        sy1(SYS_exit,0);
    }
    lg("watchdog_check\n");
    if(ui_running()){
        lg("ui_ok\n");
        writefile("/setting/zvegisk/boot_count","0\n",2);
        flushlog();
        sy1(SYS_exit,0);
    }
    lg("ui_FAIL -> reboot\n");
    /* ★ 先把我们挂的覆盖层全部卸掉，再重启。
     *   这样即使重启后 safemode/dmode 没拦住，系统文件也是干净的。 */
    rollback_mounts();
    flushlog();
    /* 强制重启：reboot(LINUX_REBOOT_CMD_RESTART) = 0x01234567 */
    sy4(SYS_reboot,0xFEE1DEAD,0x28121969,0x01234567,0);
    sy1(SYS_exit,0);
}

/* 主工作（子进程） */
static void work_inner(void)
{
    LP=0;
    /* ★ 每次开机清空日志（之后 flushlog 用 O_APPEND 追加） */
    { int tfd = sy3(SYS_open, (int)"/log/zvegisk.log", 0x241 /*O_TRUNC*/, 0644);
      if (tfd >= 0) sy1(SYS_close, tfd); }
    LOGPATH[0] = 0;                 /* 重置，让本次重新探测可用路径 */

    lg("\n===== boot uptime=");
    { static char ub[48]; int m = readfile("/proc/uptime", ub, sizeof ub);
      if (m > 0) { for (int i = 0; i < m && ub[i] != '.' && i < 20; i++) lc(ub[i]); }
      else lc('?'); }
    lg("s =====\n");
    lg("zvegisk start\n");

    /* ⓿ 自检（只写日志，不做任何部署；放在最前面以便早崩也能留下信息） */
    flushlog();          /* 先刷掉构造期可能残留的日志 */
    check_preload();
    flushlog();
    check_loop();
    check_mounts();
    flushlog();

    /* ④ 延迟自检：单独 fork 一个小进程，30 秒后再 dump 一次
     *   （此时 libObj / appFw 应已 dlopen，能看出 bind 是否生效） */
    if (sy0(SYS_fork) == 0) { check_delayed(); sy1(SYS_exit, 0); }

    /* ① safemode 检查（在 /setting 里，不依赖 SD） */
    { int fd=sy3(SYS_open,(int)"/setting/zvegisk/safemode",0,0);
      if(fd>=0){ sy1(SYS_close,fd); lg("safemode -> do nothing\n"); flushlog(); return; } }

    /* ② boot_count++ 并判断阈值 */
    static char cb[32];
    int n=readfile("/setting/zvegisk/boot_count",cb,sizeof cb);
    int cnt=0;
    if(n>0){ for(int i=0;i<n&&cb[i]>='0'&&cb[i]<='9';i++) cnt=cnt*10+(cb[i]-'0'); }
    cnt++;
    lg("boot_count=");lgd(cnt);lg("\n");
    { /* 同法：减法转十进制 */
      char o[16]; int l=0; int v=cnt;
      static const int pw[5]={10000,1000,100,10,1};
      int started=0;
      if(!v) o[l++]='0';
      else for(int k=0;k<5;k++){int d=0;while(v>=pw[k]){v-=pw[k];d++;}if(d||started){o[l++]=(char)('0'+d);started=1;}}
      o[l++]='\n';
      writefile_sync("/setting/zvegisk/boot_count",o,l); }

    if(cnt>3){
        self_disable("boot_count over limit");
        return;
    }

    /* ③ ★★ 加载模块（agent 最先行）
     *   放在挂 SD 之前：模块都在 /log（内部），不依赖 SD 是否插着。
     *   这样即使没 SD 卡、或 SD 挂了，agent 依然能起来，
     *   保证"出问题时你有远程通道"。 */
    load_modules(0);          /* 只加载代码模块（agent 是救援通道，与模式无关） */
    flushlog();

    /* ④ 挂 SD 卡 */
    if(mount_sd()!=0){ lg("sd_unavailable -> nothing\n"); flushlog(); return; }

    /* ③.5 ★ 挂载后立刻查 SD 卡上的 safemode
     *   ⇒ 相机起不来时，拔卡插电脑放个 safemode 文件就能救。 */
    { static char sp[PATHMAX];
      scpy(sp,"/tmp/zsd/zvegisk/safemode",PATHMAX);
      if (exists(sp)) {
          lg("SD safemode -> do nothing\n");
          unmount_sd();
          flushlog();
          return;
      } }

    /* ④ 读 boot_mode */
    static char bm[32];
    n=readfile("/tmp/zsd/zvegisk/boot_mode",bm,sizeof bm);
    lg("boot_mode="); lg(n>0?bm:"(none)"); lg("\n");

    /* ★★★ 立刻卸载：vfat 多重挂载会与相机自己的挂载冲突（EBUSY），
     *   所以只保持毫秒级窗口。Phase 2 若需要模块文件，先拷到 /log 再卸载。 */
    unmount_sd();

    /* ⑤ Phase 1：先只看模式，不做任何 bind */
    if(n>0 && shas(bm,"new")){
        lg("mode=NEW -> apply overlays\n");
        load_modules(1);      /* ★ 只有 new 模式才应用资源覆盖 */
        flushlog();
        /* TODO Phase 2: 遍历 /tmp/zsd/zvegisk/mods/<name>/system/ 做 mount --bind */
    } else {
        lg("mode=OLD -> stock boot\n");
    }

    flushlog();

    /* ⑥ 起 watchdog（不阻塞父进程，也不阻塞本进程退出） */
    /* ★★ 只有 armed 标志存在才起 watchdog
     *   测试场景（agent 里手动跑）绝不能留下 90 秒后重启的看门狗 */
    if (exists("/setting/zvegisk/armed")) {
        lg("armed -> start watchdog\n");
        flushlog();
        if (sy0(SYS_fork)==0){ watchdog(); }
    } else {
        lg("not armed -> no watchdog\n");
        flushlog();
    }
}

/* ★ 包装：开工前检测上次是否死在半路，收工后标记 DONE */
static void work(void)
{
    int incomplete = 0;
    { static char b[16];
      if (readfile(PHASE_FILE, b, sizeof b) > 0 && shas(b, "RUNNING")) incomplete = 1; }

    phase_set("RUNNING");

    if (incomplete) {
        int rc = read_small_int(RAPID_FILE) + 1;
        write_small_int(RAPID_FILE, rc);
        lg("!! last boot did NOT finish (rapid_fail="); lgd(rc); lg(")\n");
        if (rc >= 2) {                 /* ★ 连续 2 次死在半路 ⇒ 立刻自救 */
            self_disable("2 incomplete boots");
            return;
        }
    }

    work_inner();

    phase_set("DONE");                 /* ★ 走完了 */
    sy1(SYS_unlink, (int)RAPID_FILE);  /* 清掉快速失败计数 */
    flushlog();
}

/* ★★★ 构造函数：第一件事就是 fork，父进程立刻返回 ★★★ */
__attribute__((constructor)) static void zvegisk_init(void)
{
    int pid = sy0(SYS_fork);
    if (pid == 0) {
        /* 子进程：做所有工作。即使这里崩了也不影响 im.elf */
        work();
        sy1(SYS_exit, 0);
        for(;;){}
    }
    /* 父进程（im.elf）立刻返回，main() 照常执行 */
}
