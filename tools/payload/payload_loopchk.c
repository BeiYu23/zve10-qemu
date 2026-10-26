/* payload_loopchk.c — 通过 libJit 通道在相机上跑 loop 可用性测试
 * 纯 syscall，零 libc 依赖，结果写 /log/zvloop.log
 */
#define _GNU_SOURCE
#include <stdarg.h>

#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_mknod    14
#define SYS_mount    21
#define SYS_umount2  52
#define SYS_mkdir    39
#define SYS_nanosleep 162
#define SYS_ioctl    54

#define MS_RDONLY     1
#define MS_BIND    4096
#define LOOP_SET_FD 0x4C00UL

static inline int sy1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int sy2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int sy3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int sy4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}
static inline int sy5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory","cc");return r0;}

static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static char LB[3072]; static int LP;
static void lg(const char*s){while(*s&&LP<3060)LB[LP++]=*s++;}
static void lgd(int v){char t[12];int n=0;if(v<0){lg("-");v=-v;}if(!v){LB[LP++]='0';return;}while(v&&n<11){t[n++]='0'+(v%10);v/=10;}while(n)LB[LP++]=t[--n];}
static void fl(void){int fd=sy3(SYS_open,(int)"/log/zvloop.log",0x241,0644);if(fd<0)return;int o=0;while(o<LP){int r=sy3(SYS_write,fd,(int)(LB+o),LP-o);if(r<=0)break;o+=r;}sy1(SYS_close,fd);LP=0;}
static int exists(const char*p){int fd=sy3(SYS_open,(int)p,0,0);if(fd<0)return 0;sy1(SYS_close,fd);return 1;}

static void test(void)
{
    lg("=== loop check (payload via libJit) ===\n");
    lg("euid="); lgd(sy1(20,0)); lg("\n");

    /* ① /dev 列表里有 loop 吗 */
    lg("exists /dev/loop0 = "); lgd(exists("/dev/loop0")); lg("\n");
    lg("exists /dev/loop-control = "); lgd(exists("/dev/loop-control")); lg("\n");

    /* ② /proc 可读？ */
    { char b[256]; int fd=sy3(SYS_open,(int)"/proc/uptime",0,0); int n=-1;
      if(fd>=0){ n=sy3(SYS_read,fd,(int)b,200); sy1(SYS_close,fd); }
      lg("read /proc/uptime = "); lgd(n); lg("\n"); }

    /* ③ 挂 tmpfs（/tmp 是 vfat 不能 mknod） */
    sy2(SYS_mkdir,(int)"/tmp/zdev",0755);
    int r = sy5(SYS_mount,(int)"tmpfs",(int)"/tmp/zdev",(int)"tmpfs",0,0);
    lg("mount tmpfs = "); lgd(r); lg("\n");
    if (r != 0) { lg("  (可能已挂，继续)\n"); }

    /* ④ mknod loop0 */
    sy1(10,(int)"/tmp/zdev/loop0");
    r = sy3(SYS_mknod,(int)"/tmp/zdev/loop0",060660,(7<<8)|0);
    lg("mknod loop0 = "); lgd(r); lg("\n");
    lg("exists /tmp/zdev/loop0 = "); lgd(exists("/tmp/zdev/loop0")); lg("\n");

    /* ⑤ open loop0 */
    int lfd = sy3(SYS_open,(int)"/tmp/zdev/loop0",2,0);
    lg("open loop0(O_RDWR) = "); lgd(lfd); lg("\n");
    if (lfd < 0) { lg("=> LOOP UNAVAILABLE (open fail)\n"); fl(); return; }

    /* ⑥ 如果镜像在，做完整测试 */
    const char *img = "/log/zvegisk/m2usr.img";
    if (!exists((char*)img)) {
        lg("m2usr.img absent -> basic only\n");
        sy1(SYS_close,lfd);
        lg("=> LOOP BASIC OK (mknod+open 成功)\n");
        fl(); return;
    }
    int ifd = sy3(SYS_open,(int)img,0,0);
    lg("open image = "); lgd(ifd); lg("\n");
    if (ifd < 0) { sy1(SYS_close,lfd); fl(); return; }

    r = sy3(SYS_ioctl,lfd,LOOP_SET_FD,ifd);
    lg("LOOP_SET_FD = "); lgd(r); lg("\n");
    if (r != 0) { lg("=> LOOP UNAVAILABLE (ioctl fail)\n"); sy1(SYS_close,ifd); sy1(SYS_close,lfd); fl(); return; }

    sy2(SYS_mkdir,(int)"/tmp/m2usr",0755);
    r = sy5(SYS_mount,(int)"/tmp/zdev/loop0",(int)"/tmp/m2usr",(int)"ext2",MS_RDONLY,0);
    lg("mount ext2 = "); lgd(r); lg("\n");
    lg(r==0 ? "=> LOOP USABLE ***\n" : "=> LOOP PLAUSIBLE but mount failed\n");

    sy1(SYS_close,ifd); sy1(SYS_close,lfd);
    fl();
}

static inline int sy0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}

__attribute__((constructor)) static void entry(void)
{
    int p = sy0(SYS_fork);
    if (p == 0) { test(); sy1(SYS_exit,0); for(;;){} }
}
