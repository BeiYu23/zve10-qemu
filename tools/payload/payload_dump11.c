/* payload_dump11.c — 挂 SD 卡并把 nflasha11 备份过去
 *
 * 设备名来自 /proc/partitions 实测：
 *   SD 卡分区 = /dev/mmca10p1  (58 GB, FAT32)
 *   目标分区  = /dev/nflasha11 (500 MB, 挂载点 /log)
 *
 * 流程（全部在子进程）：
 *   1. mkdir /tmp/mnt
 *   2. mount -t vfat /dev/mmca10p1 /tmp/mnt
 *   3. 若成功：dd if=/dev/nflasha11 of=/tmp/mnt/nflasha11.img
 *   4. 写日志到 /tmp/jiritsu/probe.txt
 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_mount 21
#define SYS_fsync 118

static inline int s0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int s1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int s2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int s3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int s5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static void sapp(char*d,int*p,const char*s){while(*s)d[(*p)++]=*s++;}
static void sappd(char*d,int*p,int v){char t[12];int n=0;if(v<0){d[(*p)++]='-';v=-v;}if(!v){d[(*p)++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)d[(*p)++]=t[--n];}
static void log(char*buf,int*p,const char*k,int v){
    sapp(buf,p,k); sappd(buf,p,v); sapp(buf,p,"\r\n");
}

static void work(void){
    static char buf[1024];
    int p=0;
    sapp(buf,&p,"DUMP11_START\r\n");

    s2(39,(int)"/tmp/mnt",0755);                 /* mkdir */

    int rc=s5(SYS_mount,(int)"/dev/mmca10p1",(int)"/tmp/mnt",(int)"vfat",0,0);
    log(buf,&p,"MOUNT_RC=",rc);
    if(rc!=0){ log(buf,&p,"MOUNT_ERRNO=",-rc); sapp(buf,&p,"DUMP11_ABORT_NO_MOUNT\r\n"); }
    else {
        sapp(buf,&p,"MOUNTED\r\n");

        /* 直接 dd 等价：读 nflasha11 → 写 /tmp/mnt/nflasha11.img
           分块 64KB，边读边写 */
        int in=s3(SYS_open,(int)"/dev/nflasha11",0,0);
        log(buf,&p,"IN_FD=",in);
        if(in>=0){
            int out=s3(SYS_open,(int)"/tmp/mnt/nflasha11.img",0x241,0644);
            log(buf,&p,"OUT_FD=",out);
            if(out>=0){
                static char blk[65536];
                long long total=0;
                for(;;){
                    int n=s3(SYS_read,in,(int)blk,(int)sizeof(blk));
                    if(n<0){ log(buf,&p,"READ_ERR=",-n); break; }
                    if(n==0) break;
                    int off=0, ok=1;
                    while(off<n){
                        int w=s3(SYS_write,out,(int)(blk+off),n-off);
                        if(w<=0){ ok=0; log(buf,&p,"WRITE_ERR=",-w); break; }
                        off+=w;
                    }
                    if(!ok) break;
                    total+=n;
                    /* 每 64MB 落盘一次，防止缓冲堆积 */
                    if((total & 0x3FFFFFF)==0) s1(SYS_fsync,out);
                }
                s1(SYS_fsync,out);
                s1(SYS_close,out);
                sapp(buf,&p,"BYTES_WRITTEN=");
                /* total 以 MB 显示 */
                sappd(buf,&p,(int)(total>>20));
                sapp(buf,&p," MB\r\n");
            }
            s1(SYS_close,in);
        }
        sapp(buf,&p,"DUMP11_DONE\r\n");
    }

    int ofd=s3(SYS_open,(int)"/tmp/jiritsu/probe.txt",0x241,0644);
    if(ofd>=0){ int o=0; while(o<p){int r=s3(SYS_write,ofd,(int)(buf+o),p-o); if(r<=0)break; o+=r;} s1(SYS_close,ofd); }
    ofd=s3(SYS_open,(int)"/tmp/jiritsu/tmmc.jit",0x241,0644);
    if(ofd>=0){ int o=0; while(o<p){int r=s3(SYS_write,ofd,(int)(buf+o),p-o); if(r<=0)break; o+=r;} s1(SYS_close,ofd); }
}

__attribute__((constructor)) static void init(void){
    int pid=s0(SYS_fork);
    if(pid==0){ work(); s1(SYS_exit,0); for(;;){} }
}
