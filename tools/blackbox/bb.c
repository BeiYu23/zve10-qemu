/* bb.so —— 启动黑匣子（用户态部分）
 * 在 im.elf 的 preload 构造函数里执行，独立于 UI 是否成功。
 * 做三件事：
 *   ① 把「此刻的启动状态」写到已知空闲分区 nflasha16 的固定偏移（纯文本）
 *      ⇒ PC 用 fastboot 的 oem upload 直接读原始扇区，不需要任何驱动/UI
 *   ② 同时写 /tmp/.bb.log 和 /log/bb.log（若可写）
 *   ③ 每个 im.elf 实例都写一行（argv + 关键库尺寸），可看出 7 个实例的状态
 * 全部裸 syscall，0 外部依赖；r7 在 asm 内部装载（见 ROUND23）。
 */
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_read    3
#define SYS_lseek  19
#define SYS_getpid 20
#define SYS_init_module 128   /* ARM EABI */

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     64
#define O_APPEND  1024
#define S_IFBLK  0060000

static long sy(long n,long a,long b,long c){
    register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b;
    register long r2 __asm__("r2")=c;
    __asm__ volatile("mov r7, %[nr]\n\tsvc 0"
        : "+r"(r0) : [nr]"r"(n), "r"(r1), "r"(r2) : "r7","memory");
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
static void whex(int fd,unsigned long v,int digits){
    static const char hx[]="0123456789abcdef";
    char b[16];
    for(int i=digits-1;i>=0;i--){b[i]=hx[v&15];v>>=4;}
    w(fd,b,digits);
}
static int exists(const char*p){ long f=sy(SYS_open,(long)p,O_RDONLY,0); if(f<0)return 0; sy(SYS_close,f,0,0); return 1; }
static long fsize(const char*p){ long f=sy(SYS_open,(long)p,O_RDONLY,0); if(f<0)return -1;
    long s=sy(SYS_lseek,f,0,2); sy(SYS_close,f,0,0); return s; }

/* 在 /proc/partitions 里找某个分区的 major:minor */
static int find_part(const char *name, long *maj, long *min){
    long f=sy(SYS_open,(long)"/proc/partitions",O_RDONLY,0);
    if(f<0) return 0;
    static char buf[4096];
    long n=sy(SYS_read,f,(long)buf,sizeof buf-1);
    sy(SYS_close,f,0,0);
    if(n<=0) return 0;
    buf[n]=0;
    int nl=slen(name);
    for(long i=0;i<n;i++){
        if(buf[i]!=' ') continue;
        /* 跳过空格，读 major */
        long j=i+1; if(j>=n) break;
        long a=0; int got=0;
        while(j<n && buf[j]>='0'&&buf[j]<='9'){ a=a*10+(buf[j]-'0'); j++; got=1; }
        if(!got) continue;
        while(j<n && buf[j]==' ') j++;
        long b=0; got=0;
        while(j<n && buf[j]>='0'&&buf[j]<='9'){ b=b*10+(buf[j]-'0'); j++; got=1; }
        if(!got) continue;
        while(j<n && (buf[j]==' '||buf[j]=='\t')) j++;
        /* 比较名字 */
        int k=0; while(k<nl && j+k<n && buf[j+k]==name[k]) k++;
        if(k==nl && (j+nl>=n || buf[j+nl]=='\n' || buf[j+nl]==' ')){
            *maj=a; *min=b; return 1;
        }
    }
    return 0;
}

static void dump_state(int fd){
    ws(fd,"-- state --\n");
    /* /proc/mounts 里 /usr 与 /tmp 的行 */
    {
        long f=sy(SYS_open,(long)"/proc/mounts",O_RDONLY,0);
        if(f>=0){
            static char b[4096];
            long n=sy(SYS_read,f,(long)b,sizeof b-1);
            sy(SYS_close,f,0,0);
            if(n>0){
                b[n]=0;
                long s=0;
                for(long i=0;i<=n;i++){
                    if(i==n || b[i]=='\n'){
                        /* 这一行里是否含 " /usr " 或 " /tmp " 或 " /log " */
                        int hit=0;
                        for(long t=s;t+5<i;t++){
                            if(b[t]==' '&&b[t+1]=='/'&&b[t+2]=='u'&&b[t+3]=='s'&&b[t+4]=='r'&&(t+5==i||b[t+5]==' ')){hit=1;break;}
                            if(b[t]==' '&&b[t+1]=='/'&&b[t+2]=='t'&&b[t+3]=='m'&&b[t+4]=='p'&&(t+5==i||b[t+5]==' ')){hit=1;break;}
                            if(b[t]==' '&&b[t+1]=='/'&&b[t+2]=='l'&&b[t+3]=='o'&&b[t+4]=='g'&&(t+5==i||b[t+5]==' ')){hit=1;break;}
                        }
                        if(hit){ ws(fd,"mount: "); w(fd,b+s,(int)(i-s)); ws(fd,"\n"); }
                        s=i+1;
                    }
                }
            }
        }
    }
    /* 关键库尺寸（判断 bind/刷写是否生效） */
    { const char*p="/usr/lib/libObj.so";      ws(fd,"libObj      = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    { const char*p="/usr/lib/appFw.so";       ws(fd,"appFw       = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    { const char*p="/usr/lib/appFwBoot.so";   ws(fd,"appFwBoot   = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    { const char*p="/usr/lib/libIMDB.so";     ws(fd,"libIMDB     = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    { const char*p="/usr/lib/libInfraSKD.so"; ws(fd,"libInfraSKD = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    { const char*p="/usr/share/app/global.xdb"; ws(fd,"global.xdb  = "); wd(fd,fsize(p)); ws(fd,"\n"); }
    /* MemFree */
    {
        long f=sy(SYS_open,(long)"/proc/meminfo",O_RDONLY,0);
        if(f>=0){
            static char b[2048];
            long n=sy(SYS_read,f,(long)b,sizeof b-1);
            sy(SYS_close,f,0,0);
            if(n>0){ b[n]=0;
                for(long i=0;i+7<n;i++){
                    if(b[i]=='M'&&b[i+1]=='e'&&b[i+2]=='m'&&b[i+3]=='F'&&b[i+4]=='r'&&b[i+5]=='e'&&b[i+6]=='e'){
                        long j=i; while(j<n&&b[j]!='\n')j++;
                        ws(fd,"meminfo: "); w(fd,b+i,(int)(j-i)); ws(fd,"\n"); break;
                    }
                }
            }
        }
    }
}


/* 把 /log/blackbox.ko 读进内存并 init_module。返回 0 成功。
 * 缓冲区放 .bss（64 KB），不占栈。 */
static char kobuf[65536];
static long load_blackbox(const char *ko, const char *params){
    long f = sy(SYS_open,(long)ko,O_RDONLY,0);
    if (f < 0) return -1;
    long n = 0, r;
    while (n < (long)sizeof kobuf - 1){
        r = sy(SYS_read, f, (long)(kobuf + n), sizeof kobuf - 1 - n);
        if (r <= 0) break;
        n += r;
    }
    sy(SYS_close, f, 0, 0);
    if (n <= 0) return -2;
    /* 已经是 .ko 且在内存里 ⇒ 直接 init_module */
    return sy(SYS_init_module, (long)kobuf, n, (long)params);
}

__attribute__((constructor)) static void go(void){
    long maj=0,min=0;
    int have_part = find_part("nflasha16",&maj,&min);
    /* 节点不存在就建（major:minor 来自 /proc/partitions） */
    const char *dev = "/dev/bb16";
    if(have_part && !exists(dev)){
        /* ARM EABI: __NR_mknod = 14（不是 128，128 是 init_module） */
        sy(14 /*mknod*/,(long)dev,S_IFBLK|0600,
           (int)(((maj&0xfff)<<8)|(min&0xff)|((min&~0xff)<<12)));
    }
    /* 先把内核模块加载起来（它要打开 /dev/bb16，所以节点必须先建好） */
    long mret = load_blackbox("/log/blackbox.ko", "dev=/dev/bb16");
    /* 文本写到三个地方：nflasha16 原始扇区、/tmp、/log */
    long pos = 0;
    for(int dst=0;dst<3;dst++){
        const char *path; long fd;
        if(dst==0){ if(!have_part) continue; path=dev; }
        else if(dst==1) path="/tmp/.bb.log";
        else            path="/log/bb.log";
        fd = sy(SYS_open,(long)path, (dst==0)?O_RDWR:(O_WRONLY|O_CREAT|O_APPEND), 0666);
        if(fd<0) continue;
        if(dst==0){
            /* 每个 im.elf 实例一个 1 KB 槽位：槽号 = pid%8（用减法避免除法）*/
            long pid = sy(20 /*getpid*/,0,0,0);
            long slot = pid; while(slot>=8) slot-=8;   /* slot = pid mod 8 */
            sy(SYS_lseek,fd,slot*1024,0);
        }
        ws(fd,"BB1 pid=");
        wd(fd, sy(20 /*getpid*/,0,0,0));
        ws(fd," slot=");
        { long pid=sy(20,0,0,0); long sl=pid; while(sl>=8) sl-=8; wd(fd,sl); }
        ws(fd,"\n");
        dump_state(fd);
        ws(fd,"modload=");
        wd(fd, mret);
        ws(fd," (0=ok, -1=no ko, -2=empty, <-2=errno)\n");
        ws(fd,"-- end --\n");
        sy(SYS_close,fd,0,0);
        pos++;
    }
    (void)pos;
}
