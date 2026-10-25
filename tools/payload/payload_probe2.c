/* payload_probe2.c — Stage 1b：找 SD 卡真实设备名
 *
 * 依据上一轮结果：
 *   /dev/mmcaa → ENXIO（不存在）
 *   /dev/mmca  → EINVAL（存在，但整卡挂载失败 ⇒ 卡上有分区表）
 * 所以本轮：读 /proc/partitions 列出全部块设备，并逐个试挂载
 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_mount 21

static inline int sys0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sys1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sys2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sys3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sys5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static void sapp(char*d,int*p,const char*s){while(*s)d[(*p)++]=*s++;}
static void sappd(char*d,int*p,int v){char t[12];int n=0;if(v<0){d[(*p)++]='-';v=-v;}if(!v){d[(*p)++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)d[(*p)++]=t[--n];}
static int open_rd(const char*path){return sys3(SYS_open,(int)path,0,0);}
static int open_w(const char*path){return sys3(SYS_open,(int)path,0x241,0644);}
static void wr(int fd,const char*s,int n){int o=0;while(o<n){int r=sys3(SYS_write,fd,(int)(s+o),n-o);if(r<=0)break;o+=r;}}

/* 把一个文本文件整体读进 buf，返回字节数（-1 失败） */
static int slurp(const char*path,char*buf,int cap){
    int fd=open_rd(path); if(fd<0)return -1;
    int n=sys3(SYS_read,fd,(int)buf,cap-1); sys1(SYS_close,fd);
    if(n<0)return -1; buf[n]=0; return n;
}

static void work(void){
    static char buf[3072]; static char tmp[1400];
    int p=0;
    sapp(buf,&p,"PROBE2_OK\r\n");

    /* ① /proc/partitions —— 全部块设备 */
    int n=slurp("/proc/partitions",tmp,sizeof(tmp));
    sapp(buf,&p,"PARTITIONS_RC="); sappd(buf,&p,n); sapp(buf,&p,"\r\n");
    if(n>0){
        sapp(buf,&p,"--- PARTITIONS BEGIN ---\r\n");
        for(int i=0;i<n&&p<(int)sizeof(buf)-4;i++){
            if(tmp[i]=='\n'){buf[p++]='\r';buf[p++]='\n';} else buf[p++]=tmp[i];
        }
        sapp(buf,&p,"--- PARTITIONS END ---\r\n");
    }

    /* ② 逐个候选设备试挂载（只读尝试，挂不上就跳过） */
    static const char*devs[]={
        "/dev/mmcab","/dev/mmcac","/dev/mmcad",
        "/dev/mmcb1","/dev/mmcba","/dev/mmcbb",
        "/dev/mmcblk0p1","/dev/sda1", 0 };
    for(int i=0;devs[i];i++){
        int r=sys5(SYS_mount,(int)devs[i],(int)"/tmp/mnt",(int)"vfat",0,0);
        sapp(buf,&p,"MNT "); sapp(buf,&p,devs[i]);
        sapp(buf,&p," rc="); sappd(buf,&p,r);
        if(r==0){ sapp(buf,&p," ★OK\r\n"); }
        else { sapp(buf,&p," e="); sappd(buf,&p,-r); sapp(buf,&p,"\r\n"); }
    }

    /* ③ 挂载成功的话，列一下根目录 */
    int fd=open_rd("/tmp/mnt");   /* 目录 open 成功说明挂上了 */
    sapp(buf,&p,"MNT_ROOT_OPEN="); sappd(buf,&p,fd); sapp(buf,&p,"\r\n");
    if(fd>=0) sys1(SYS_close,fd);

    sapp(buf,&p,"PROBE2_DONE\r\n");

    int ofd=open_w("/tmp/jiritsu/probe.txt");
    if(ofd>=0){ wr(ofd,buf,p); sys1(SYS_close,ofd); }
    ofd=open_w("/tmp/jiritsu/tmmc.jit");
    if(ofd>=0){ wr(ofd,buf,p); sys1(SYS_close,ofd); }
}

__attribute__((constructor)) static void init(void){
    int pid=sys0(SYS_fork);
    if(pid==0){
        sys2(39,(int)"/tmp/mnt",0755);   /* mkdir，EEXIST 无害 */
        work();
        sys1(SYS_exit,0);
        for(;;){}
    }
}
