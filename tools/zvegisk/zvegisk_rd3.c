/* zvegisk_rd3.c — 对指定 64KB 块做精确 u32 差分
 * 用法: zvegisk_rd3 <块号> [等待秒数]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>

#define OUT  "/log/zvegisk_rd3.log"
#define DEV  "/dev/stream"
#define OFF  0x20312000UL
#define LEN  0x2000000UL
#define CHUNK 0x10000UL

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(int argc, char **argv)
{
    wrr();
    int blk = (argc>1)?atoi(argv[1]):0;
    int wait_s = (argc>2)?atoi(argv[2]):6;
    logf("RD3_START block=%d wait=%ds uid=%d", blk, wait_s, geteuid());

    int fd = open(DEV, O_RDONLY);
    if (fd<0){logf("open FAIL %s",strerror(errno));logf("RD3_DONE");return 1;}
    unsigned char *b = mmap(NULL,LEN,PROT_READ,MAP_SHARED,fd,(off_t)OFF);
    if (b==MAP_FAILED){logf("mmap FAIL %s",strerror(errno));close(fd);logf("RD3_DONE");return 1;}
    unsigned char *p = b + (unsigned long)blk*CHUNK;

    unsigned char *A = malloc(CHUNK); if(!A){logf("malloc FAIL");return 1;}
    memcpy(A, p, CHUNK);
    logf("快照 A 已取（块 %d，off=0x%lx）", blk, (unsigned long)blk*CHUNK);

    /* 打印 A 里非零 u32 的位置（帮助识别结构） */
    logf("--- A 中前 64 个非零 u32 ---");
    { int n=0;
      for (unsigned long o=0;o<CHUNK && n<64;o+=4){
        unsigned int v; memcpy(&v,A+o,4);
        if (v){ logf("  +0x%04lx 0x%08x", o, v); n++; }
      }
      logf("  (共列出 %d 个)", n); }

    logf("");
    logf(">>> 请【现在按一次 DOWN】，等 %d 秒", wait_s);
    sync(); sleep(wait_s);

    logf("");
    logf("=== 精确差分（u32）===");
    int cnt=0;
    for (unsigned long o=0;o+4<=CHUNK;o+=4){
        unsigned int va,vb; memcpy(&va,A+o,4); memcpy(&vb,p+o,4);
        if (va!=vb){
            logf("  +0x%04lx  0x%08x -> 0x%08x   (diff %d)", o, va, vb, (int)(vb-va));
            cnt++;
            if (cnt>=200){ logf("  ...(截断)"); break; }
        }
    }
    logf("--- 变化 u32 总数: %d ---", cnt);
    if (cnt>1 && cnt<=60){
        logf("--- 间距 ---");
        unsigned long prev=0; int first=1;
        for (unsigned long o=0;o+4<=CHUNK;o+=4){
            unsigned int va,vb; memcpy(&va,A+o,4); memcpy(&vb,p+o,4);
            if (va!=vb){ if(!first && o-prev!=4) logf("    +0x%lx -> +0x%lx 间距 %lu", prev,o,o-prev); prev=o; first=0; }
        }
    }
    free(A); munmap(b,LEN); close(fd);
    logf("RD3_DONE");
    return 0;
}
