/* bindm2.so —— 在 im.elf 的 preload 构造函数里
 *   把 /log/m2usr.img（M2 的 /usr 镜像，放在可写的 /log 分区）loop 挂载，
 *   再把它的 lib/ 和 share/ bind 到 /usr/lib 和 /usr/share。
 * 时机：preload 构造函数在 im.elf 的 main 之前执行，
 *       而 libIMDB/appFw 是在 im.elf 里 dlopen 的 ⇒ bind 一定先于它们。
 * 全部裸 syscall，不 hook、不 exit；任何失败都只记日志然后安静返回。
 */
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_ioctl  54
#define SYS_mount  21
#define SYS_mkdir  39
#define SYS_mknod  14
#define SYS_unlink 10
#define SYS_read    3
#define SYS_lseek  19

#define LOOP_SET_FD       0x4C00
#define LOOP_CLR_FD       0x4C01
#define LOOP_CTL_GET_FREE 0x4C82
#define MS_RDONLY         1
#define MS_BIND        0x1000
#define O_RDWR            2
#define O_RDONLY          0
#define S_IFBLK           0060000
#define S_IFCHR           0020000
#define MKDEV(a,b)        (((a)<<8)|(b))

/* ★ 关键：r7 必须在 asm 内部显式装载，并列入 clobber。
 *   否则 GCC 视 r7 为 callee-saved，会跨函数调用复用已过期的值
 *   （实测过：两次 bind 的 r7 被污染成 5=open，导致 mount 变成 open） */
static long sy(long n,long a,long b,long c){
    register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b;
    register long r2 __asm__("r2")=c;
    __asm__ volatile(
        "mov r7, %[nr]\n\t"
        "svc 0"
        : "+r"(r0)
        : [nr]"r"(n), "r"(r1), "r"(r2)
        : "r7","memory");
    return r0;
}
static long sy5(long n,long a,long b,long c,long d,long e){
    register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b;
    register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d;
    register long r4 __asm__("r4")=e;
    __asm__ volatile(
        "mov r7, %[nr]\n\t"
        "svc 0"
        : "+r"(r0)
        : [nr]"r"(n), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
        : "r7","memory");
    return r0;
}
static int slen(const char*s){int n=0;if(!s)return 0;while(s[n])n++;return n;}
static void w(int fd,const char*s,int n){if(fd>=0&&n>0)sy(SYS_write,fd,(long)s,n);}
static void ws(int fd,const char*s){w(fd,s,slen(s));}
static void wd(int fd,long v){
    char b[16];int n=0,st=0;
    static const long pw[10]={1000000000L,100000000L,10000000L,1000000L,100000L,10000L,1000L,100L,10L,1L};
    if(v<0){ws(fd,"-");v=-v;}
    if(v==0){ws(fd,"0");return;}
    for(int i=0;i<10;i++){int d=0;while(v>=pw[i]){v-=pw[i];d++;if(d>9)break;}
        if(d||st){b[n++]='0'+d;st=1;}}
    w(fd,b,n);
}
static void logkv(const char*k,long v){
    long fd=sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
    if(fd<0) fd=sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
    if(fd<0) return;
    ws(fd,k); wd(fd,v); ws(fd,"\n");
    sy(SYS_close,fd,0,0);
}
static void logmsg(const char*s){
    long fd=sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
    if(fd<0) fd=sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
    if(fd<0) return;
    ws(fd,s); ws(fd,"\n");
    sy(SYS_close,fd,0,0);
}

/* 在文件中查找子串（用于判断是否已经挂载过） */
static int file_has(const char *path, const char *needle){
    long f = sy(SYS_open,(long)path,0,0);
    if (f < 0) return 0;
    static char buf[8192];
    long n = sy(SYS_read, f, (long)buf, sizeof buf - 1);
    sy(SYS_close, f, 0, 0);
    if (n <= 0) return 0;
    buf[n] = 0;
    int nl = slen(needle);
    for (long i = 0; i + nl <= n; i++){
        int ok = 1;
        for (int k = 0; k < nl; k++) if (buf[i+k] != needle[k]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}

static int exists(const char*p){ long f=sy(SYS_open,(long)p,0,0); if(f<0) return 0; sy(SYS_close,f,0,0); return 1; }


/* 用 open + lseek(END) 取文件大小，避免引入 stat 结构体 */
static long fsize(const char *path){
    long f = sy(SYS_open,(long)path,0,0);
    if (f < 0) return -1;
    long sz = sy(SYS_lseek, f, 0, 2);   /* SEEK_END = 2 */
    sy(SYS_close, f, 0, 0);
    return sz;
}
/* 把 /proc 里含 key 的一行写进 /log */
static void log_line(const char *path, const char *key){
    long f = sy(SYS_open,(long)path,0,0);
    if (f < 0) return;
    static char buf[2048];
    long n = sy(SYS_read, f, (long)buf, sizeof buf - 1);
    sy(SYS_close, f, 0, 0);
    if (n <= 0) return;
    buf[n] = 0;
    int kl = slen(key);
    for (long i = 0; i + kl <= n; i++){
        int ok = 1;
        for (int k = 0; k < kl; k++) if (buf[i+k] != key[k]) { ok = 0; break; }
        if (ok){
            long j = i; while (j < n && buf[j] != '\n') j++;
            long fd = sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
            if (fd < 0) fd = sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
            if (fd >= 0){ w(fd, buf+i, (int)(j-i)); ws(fd,"\n"); sy(SYS_close,fd,0,0); }
            return;
        }
    }
}
/* 写一行 "label<size>" */
static void logsz(const char *label, const char *path){
    long fd = sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
    if (fd < 0) fd = sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
    if (fd < 0) return;
    ws(fd, label); wd(fd, fsize(path)); ws(fd, "\n");
    sy(SYS_close,fd,0,0);
}


/* cmdline 是 \0 分隔的，单独处理：全读、NUL 换空格 */
static void log_cmdline(void){
    long f = sy(SYS_open,(long)"/proc/self/cmdline",0,0);
    if (f < 0) return;
    static char b[512];
    long n = sy(SYS_read, f, (long)b, sizeof b - 1);
    sy(SYS_close, f, 0, 0);
    if (n <= 0) return;
    for (long i = 0; i < n; i++) if (b[i] == 0) b[i] = ' ';
    b[n] = 0;
    long fd = sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
    if (fd < 0) fd = sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
    if (fd < 0) return;
    ws(fd,"argv: "); w(fd,b,(int)n); ws(fd,"\n");
    sy(SYS_close,fd,0,0);
}
/* 读块设备/文件在 offset 处的 2 字节，用于确认 ext2 magic */
static void log_magic(const char *dev){
    long f = sy(SYS_open,(long)dev,0,0);
    if (f < 0){ logmsg("magic: open fail"); return; }
    sy(SYS_lseek, f, 1080, 0);          /* SEEK_SET=0 */
    unsigned char m[2] = {0,0};
    long n = sy(SYS_read, f, (long)m, 2);
    sy(SYS_close, f, 0, 0);
    long fd = sy(SYS_open,(long)"/log/bindm2.log",1089,0666);
    if (fd < 0) fd = sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
    if (fd < 0) return;
    ws(fd,"ext2_magic: ");
    if (n == 2){
        static const char hex[]="0123456789abcdef";
        char h[5]; h[0]=hex[m[0]>>4]; h[1]=hex[m[0]&15]; h[2]=hex[m[1]>>4]; h[3]=hex[m[1]&15]; h[4]=0;
        ws(fd,h); ws(fd, (m[0]==0x53 && m[1]==0xef) ? "  => 是 ext2 ✓\n" : "  => ★不是 ext2（镜像无效）\n");
    } else ws(fd,"read fail\n");
    sy(SYS_close,fd,0,0);
}

__attribute__((constructor)) static void go(void){
    /* 只在镜像存在时动作；没有镜像就安静离开（正常老菜单启动不受影响） */
    /* 依次尝试多个位置：/log（500MB 分区）→ /sd（SD 卡，若有）→ /tmp */
    const char *imgpath = 0;
    if (exists("/log/m2usr.img"))      imgpath = "/log/m2usr.img";
    else if (exists("/sd/m2usr.img"))  imgpath = "/sd/m2usr.img";
    else if (exists("/tmp/m2usr.img")) imgpath = "/tmp/m2usr.img";
    if (!imgpath) return;
    /* 幂等：im.elf 有 7 个实例，每个都会跑 preload 构造函数；只允许第一个真正挂载 */
    if(file_has("/proc/mounts","m2root")) return;
    logmsg("=== bindm2 start ===");
    { long fd=sy(SYS_open,(long)"/log/bindm2.log",1089,0666); if(fd<0) fd=sy(SYS_open,(long)"/tmp/.bindm2.log",1089,0666);
      if(fd>=0){ ws(fd,"image: "); ws(fd,imgpath); ws(fd,"\n"); sy(SYS_close,fd,0,0);} }

    sy(SYS_mkdir,(long)"/tmp/m2root",0777,0);
    sy(SYS_mkdir,(long)"/tmp/m2bind",0777,0);

    /* 拿一个空闲 loop */
    long ctl=sy(SYS_open,(long)"/dev/loop-control",O_RDWR,0);
    long loopno=-1;
    if(ctl>=0){ loopno=sy(SYS_ioctl,ctl,LOOP_CTL_GET_FREE,0); sy(SYS_close,ctl,0,0); }
    if(loopno<0){
        /* 退路：直接用 /dev/loop7 */
        sy(SYS_mknod,(long)"/dev/loop7",S_IFBLK|0600,MKDEV(7,7));
        loopno=7;
    }
    logkv("loopno=",loopno);

    /* 不用除法：loopno 只可能是 0..9 或 7；>9 时用减法求十位 */
    char lp[32]; { const char*p="/dev/loop"; int i=0; while(p[i]){lp[i]=p[i];i++;} 
        long t=loopno; int tens=0;
        while(t>=10){ t-=10; tens++; }
        if(tens){ lp[i]='0'+tens; lp[i+1]='0'+t; lp[i+2]=0; }
        else { lp[i]='0'+t; lp[i+1]=0; } }
    if(!exists(lp)) sy(SYS_mknod,(long)lp,S_IFBLK|0600,MKDEV(7,loopno));

    long img=sy(SYS_open,(long)imgpath,O_RDONLY,0);
    if(img<0){ logmsg("open image FAIL"); return; }
    long lf=sy(SYS_open,(long)lp,O_RDWR,0);
    if(lf<0){ logmsg("open loopdev FAIL"); sy(SYS_close,img,0,0); return; }
    long r=sy(SYS_ioctl,lf,LOOP_SET_FD,img);
    logkv("LOOP_SET_FD=",r);
    if(r<0){ sy(SYS_close,lf,0,0); sy(SYS_close,img,0,0); return; }

    /* 挂 ext2 到 /tmp/m2root */
    r=sy5(SYS_mount,(long)lp,(long)"/tmp/m2root",(long)"ext2",MS_RDONLY,0);
    logkv("mount ext2=",r);
    if(r<0){
        log_magic(lp);                       /* 区分「镜像无效」与「权限问题」 */
        sy(SYS_ioctl,lf,LOOP_CLR_FD,0); sy(SYS_close,lf,0,0); sy(SYS_close,img,0,0); return;
    }
    logmsg("ext2 mounted");

    /* bind lib 与 share */
    r=sy5(SYS_mount,(long)"/tmp/m2root/lib",(long)"/usr/lib",0,MS_BIND,0);   /* fstype=NULL, flags=MS_BIND */
    logkv("bind /usr/lib=",r);
    r=sy5(SYS_mount,(long)"/tmp/m2root/share",(long)"/usr/share",0,MS_BIND,0);
    logkv("bind /usr/share=",r);
    /* ★ 自诊断：bind 之后这些文件的大小应变成 M2 版 */
    logmsg("-- after bind --");
    logsz("libObj.so      =", "/usr/lib/libObj.so");       /* 期望 3931624（E10 是 21305528）*/
    logsz("appFw.so       =", "/usr/lib/appFw.so");        /* 期望 64273528（E10 不存在=-1）*/
    logsz("appFwBoot.so   =", "/usr/lib/appFwBoot.so");    /* 期望 8312（E10 不存在）*/
    logsz("libIMDB.so     =", "/usr/lib/libIMDB.so");      /* 期望 40596（E10 是 37204）*/
    logsz("libInfraSKD.so =", "/usr/lib/libInfraSKD.so");  /* 期望 7259708（E10 是 10184）*/
    logsz("global.xdb     =", "/usr/share/app/global.xdb");/* 期望 16112（E10 是 12252）*/
    log_cmdline();
    log_line("/proc/meminfo", "MemFree");
    logmsg("=== bindm2 done ===");
}
