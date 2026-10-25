/* zvegisk_memprobe.c — 验证能否读写 im.elf 的内存
 *
 * 目标：
 *   ① 找到 libObj.so 在 im.elf 地址空间里的代码段基址
 *   ② pread 读 ELF 头，确认读得到
 *   ③ 计算 GRM_gpermFillrect 的运行时地址
 *   ④ 读它开头的指令，确认对得上
 *   ★ 全程只读，不写任何内存
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>

#define OUT "/log/zvegisk_mem.log"

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

/* GRM_gpermFillrect 在 libObj.so 里的 vaddr（从符号表读到的） */
#define FILLRECT_VADDR 0x683f4cUL
#define RECTBLIT_VADDR 0x684086UL

/* 找 /proc/PID/maps 里某个 .so 的 r-xp 段起始地址 */
static unsigned long find_so_base(int pid, const char *soname, int *found)
{
    char path[64]; snprintf(path,sizeof path,"/proc/%d/maps",pid);
    FILE *f=fopen(path,"r"); *found=0;
    if(!f) return 0;
    char line[512];
    while(fgets(line,sizeof line,f)){
        if(!strstr(line,soname)) continue;
        if(!strstr(line,"r-xp")) continue;
        unsigned long s=0,e=0; unsigned long off=0;
        if(sscanf(line,"%lx-%lx %*s %lx",&s,&e,&off)==3){
            *found=1; fclose(f); return s;   /* 代码段起始运行时地址 */
        }
    }
    fclose(f); return 0;
}

/* pread 若干字节 */
static int rd(int fd, unsigned long addr, unsigned char *buf, int n)
{
    ssize_t r = pread(fd, buf, n, (off_t)addr);
    return (int)r;
}

int main(int argc, char **argv)
{
    wrr();
    logf("MEMPROBE_START uid=%d", geteuid());

    int pid = (argc > 1) ? atoi(argv[1]) : 157;
    logf("target pid=%d", pid);

    int found = 0;
    unsigned long base = find_so_base(pid, "libObj.so", &found);
    logf("libObj r-xp base = 0x%lx (found=%d)", base, found);
    if (!found) { logf("NO_LIBOBJ_MAPPING"); logf("MEMPROBE_DONE"); return 1; }

    char mpath[64]; snprintf(mpath,sizeof mpath,"/proc/%d/mem",pid);
    int fd = open(mpath, O_RDONLY);
    logf("open %s = %d%s", mpath, fd, fd<0?strerror(errno):"");
    if (fd < 0) { logf("MEMPROBE_DONE"); return 1; }

    /* ① 读 ELF 头 */
    unsigned char hdr[16];
    int n = rd(fd, base, hdr, 16);
    logf("read ELF hdr @0x%lx = %d B", base, n);
    if (n > 0) {
        char hx[40]={0}; for(int i=0;i<n;i++) sprintf(hx+i*2,"%02x",hdr[i]);
        logf("  bytes: %s", hx);
        logf("  is ELF: %s", (hdr[0]==0x7f&&hdr[1]=='E'&&hdr[2]=='L'&&hdr[3]=='F')?"YES ★":"no");
    }

    /* ② 计算并读 GRM_gpermFillrect */
    unsigned long fr = base + FILLRECT_VADDR;
    unsigned char code[16];
    n = rd(fd, fr, code, 16);
    logf("GRM_gpermFillrect @0x%lx read=%d", fr, n);
    if (n > 0) {
        char hx[40]={0}; for(int i=0;i<n;i++) sprintf(hx+i*2,"%02x",code[i]);
        logf("  bytes: %s", hx);
        logf("  expect(e92d4ff0 = push {r4-r9,sl,fp,lr}) : %s",
             (code[0]==0xe9&&code[1]==0x2d&&code[2]==0x4f&&code[3]==0xf0)?"MATCH ★":"differs");
    }

    /* ③ 同样读 Rectblit */
    unsigned long rb = base + RECTBLIT_VADDR;
    n = rd(fd, rb, code, 8);
    logf("GRM_gpermRectblit   @0x%lx read=%d", rb, n);
    if (n > 0) {
        char hx[24]={0}; for(int i=0;i<n;i++) sprintf(hx+i*2,"%02x",code[i]);
        logf("  bytes: %s", hx);
    }

    close(fd);
    logf("MEMPROBE_DONE (read-only, nothing written)");
    return 0;
}
