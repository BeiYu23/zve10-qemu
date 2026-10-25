/* payload_mountsd.c — 不需 mknod：整卡 /dev/mmca + offset= 挂载 SD 分区
 *
 * 背景：/dev 只读(EROFS)，可写分区全是 vfat(不支持设备节点) ⇒ 无法 mknod。
 * 但 /dev/mmca（整卡，250:0）节点存在，且 vfat 支持 offset= 挂载选项。
 *
 * 流程：
 *   1. 读 /dev/mmca 前 512 字节（MBR），解析第一个分区起始 LBA
 *   2. mount -t vfat -o offset=<LBA*512> /dev/mmca /tmp/mnt
 *   3. 若成功 → dump /dev/nflasha11 到 /tmp/mnt/nflasha11.img
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
static inline int s4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int s5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static void sapp(char*d,int*p,const char*s){while(*s)d[(*p)++]=*s++;}
static void sappd(char*d,int*p,long v){char t[16];int n=0;if(v<0){d[(*p)++]='-';v=-v;}if(!v){d[(*p)++]='0';return;}while(v&&n<15){t[n++]='0'+(int)(v%10);v/=10;}while(n)d[(*p)++]=t[--n];}
static void logk(char*buf,int*p,const char*k,long v){sapp(buf,p,k);sappd(buf,p,v);sapp(buf,p,"\r\n");}

static unsigned le32(const unsigned char*b){return (unsigned)b[0]|((unsigned)b[1]<<8)|((unsigned)b[2]<<16)|((unsigned)b[3]<<24);}

static void work(void){
    static char buf[1024]; static char opts[64];
    int p=0;
    sapp(buf,&p,"MOUNTSD_START\r\n");
    s2(39,(int)"/tmp/mnt",0755);

    /* ① 读 /dev/mmca 的 MBR */
    int fd=s3(SYS_open,(int)"/dev/mmca",0,0);
    logk(buf,&p,"MMCA_FD=",fd);
    unsigned long offset=0; int part_ok=0;
    if(fd>=0){
        unsigned char mbr[512];
        int n=s3(SYS_read,fd,(int)mbr,512);
        s1(SYS_close,fd);
        logk(buf,&p,"MBR_READ=",n);
        if(n==512){
            logk(buf,&p,"MBR_SIG=",(mbr[510]==0x55&&mbr[511]==0xAA)?1:0);
            /* 取第一个非空分区项 */
            for(int i=0;i<4;i++){
                const unsigned char*e=mbr+446+i*16;
                unsigned type=e[4];
                unsigned lba=le32(e+8);
                if(type!=0){
                    logk(buf,&p,"PART_TYPE=",(long)type);
                    logk(buf,&p,"PART_LBA=",(long)lba);
                    if(!part_ok){ offset=(unsigned long)lba*512UL; part_ok=1; }
                }
            }
        }
    }
    logk(buf,&p,"OFFSET_BYTES=",(long)offset);

    /* ② tmpfs + mknod：/dev 只读、可写分区全是 vfat 都不支持设备节点，
          但设备节点放哪都行 —— 挂个 tmpfs 再造节点即可 */
    s2(39,(int)"/tmp/dev",0755);
    int trc=s5(SYS_mount,(int)"tmpfs",(int)"/tmp/dev",(int)"tmpfs",0,0);
    logk(buf,&p,"TMPFS_RC=",trc);

    if(trc==0){
        unsigned dev=(250u<<8)|11u;         /* mmca10p1 = 250:11 */
        int mr=s4(14 /*mknod*/,(int)"/tmp/dev/p1",060660,(int)dev,0);
        logk(buf,&p,"MKNOD_RC=",mr);        /* -17=EEXIST 也算成功 */

        int rc=s5(SYS_mount,(int)"/tmp/dev/p1",(int)"/tmp/mnt",(int)"vfat",0,0);
        logk(buf,&p,"MOUNT_RC=",rc);
        if(rc!=0){ logk(buf,&p,"MOUNT_ERRNO=",-rc); sapp(buf,&p,"MOUNTSD_ABORT\r\n"); }
        else {
            sapp(buf,&p,"MOUNTED\r\n");
            int in=s3(SYS_open,(int)"/dev/nflasha11",0,0);
            logk(buf,&p,"IN_FD=",in);
            if(in>=0){
                int out=s3(SYS_open,(int)"/tmp/mnt/nflasha11.img",0x241,0644);
                logk(buf,&p,"OUT_FD=",out);
                if(out>=0){
                    static char blk[65536]; long long tot=0;
                    for(;;){
                        int n=s3(SYS_read,in,(int)blk,(int)sizeof(blk));
                        if(n<0){ logk(buf,&p,"READ_ERR=",-n); break; }
                        if(n==0) break;
                        int o=0,ok=1;
                        while(o<n){int w=s3(SYS_write,out,(int)(blk+o),n-o); if(w<=0){ok=0;logk(buf,&p,"WRITE_ERR=",-w);break;} o+=w;}
                        if(!ok) break;
                        tot+=n;
                        if((tot & 0x3FFFFFF)==0) s1(SYS_fsync,out);
                    }
                    s1(SYS_fsync,out); s1(SYS_close,out);
                    sapp(buf,&p,"BYTES_MB="); sappd(buf,&p,(long)(tot>>20)); sapp(buf,&p,"\r\n");
                }
                s1(SYS_close,in);
            }
            sapp(buf,&p,"MOUNTSD_DONE\r\n");
        }
    }

    int ofd=s3(SYS_open,(int)"/tmp/jiritsu/probe.txt",0x241,0644);
    if(ofd>=0){int o=0;while(o<p){int r=s3(SYS_write,ofd,(int)(buf+o),p-o);if(r<=0)break;o+=r;}s1(SYS_close,ofd);}
    ofd=s3(SYS_open,(int)"/tmp/jiritsu/tmmc.jit",0x241,0644);
    if(ofd>=0){int o=0;while(o<p){int r=s3(SYS_write,ofd,(int)(buf+o),p-o);if(r<=0)break;o+=r;}s1(SYS_close,ofd);}
}

__attribute__((constructor)) static void init(void){
    int pid=s0(SYS_fork);
    if(pid==0){ work(); s1(SYS_exit,0); for(;;){} }
}
