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

/* ---- 直接 syscall ---- */
static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

/* ---- 字符串工具 ---- */
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static int shas(const char*s,const char*sub){int i,j;for(i=0;s[i];i++){for(j=0;sub[j]&&s[i+j]==sub[j];j++);if(!sub[j])return 1;}return 0;}
static void scpy(char*d,const char*s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]=0;}
static void scat(char*d,const char*s,int max){int l=slen(d),i=0;while(s[i]&&l+i<max-1){d[l+i]=s[i];i++;}d[l+i]=0;}

/* ---- 日志 ---- */
#define LOGPATH "/log/zvegisk.log"
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
static void lgx(unsigned v){static const char h[]="0123456789abcdef";lg("0x");for(int s=28;s>=0;s-=4)LB[LP++]=h[(v>>s)&0xF];}
static void flushlog(void){
    int fd=sy3(SYS_open,(int)LOGPATH,0x241,0644);
    if(fd<0)return;
    int o=0; while(o<LP){int r=sy3(SYS_write,fd,(int)(LB+o),LP-o);if(r<=0)break;o+=r;}
    sy1(SYS_close,fd);
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
    if(sy5(SYS_mount,(int)"tmpfs",(int)"/tmp/zdev",(int)"tmpfs",0,0)){lg("tmpfs_fail\n");return -1;}

    sy1(SYS_unlink,(int)"/tmp/zdev/p1");
    /* dev_t: (major<<8)|minor（小号够用） */
    unsigned dev=((unsigned)maj<<8)|(unsigned)min;
    if(sy3(SYS_mknod,(int)"/tmp/zdev/p1",(060660),(int)dev)){lg("mknod_fail\n");return -1;}

    sy2(SYS_mkdir,(int)"/tmp/zsd",0755);
    /* ★ 实测：不带任何选项才能读写 */
    if(sy5(SYS_mount,(int)"/tmp/zdev/p1",(int)"/tmp/zsd",(int)"vfat",0,0)){
        lg("mount_fail\n");return -1;
    }
    lg("sd_mounted\n");
    return 0;
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
    flushlog();
    /* 强制重启：reboot(LINUX_REBOOT_CMD_RESTART) = 0x01234567 */
    sy4(SYS_reboot,0xFEE1DEAD,0x28121969,0x01234567,0);
    sy1(SYS_exit,0);
}

/* 主工作（子进程） */
static void work(void)
{
    LP=0;
    lg("zvegisk start\n");

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
      writefile("/setting/zvegisk/boot_count",o,l); }

    if(cnt>3){
        lg("over limit -> safemode, do nothing\n");
        writefile("/setting/zvegisk/safemode","1\n",2);
        flushlog();
        return;
    }

    /* ③ 挂 SD 卡 */
    if(mount_sd()!=0){ lg("sd_unavailable -> nothing\n"); flushlog(); return; }

    /* ④ 读 boot_mode */
    static char bm[32];
    n=readfile("/tmp/zsd/zvegisk/boot_mode",bm,sizeof bm);
    lg("boot_mode="); lg(n>0?bm:"(none)"); lg("\n");

    /* ⑤ Phase 1：先只看模式，不做任何 bind */
    if(n>0 && shas(bm,"new")){
        lg("mode=NEW (Phase1: bind 未实现，只记录)\n");
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
