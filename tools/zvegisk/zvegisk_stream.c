/* zvegisk_stream.c — 验证 /dev/stream @ 0x20312000 是不是活的绘制面
 *
 * 只读映射（PROT_READ），不写任何东西。
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

#define OUT "/log/zvegisk_stream.log"
#define DEV "/dev/stream"
#define OFF 0x20312000UL
#define LEN (32UL*1024*1024)

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(void)
{
    wrr();
    logf("STREAM_PROBE_START uid=%d", geteuid());
    int fd = open(DEV, O_RDONLY);
    logf("open %s O_RDONLY = %d %s", DEV, fd, fd<0?strerror(errno):"");
    if (fd < 0) { logf("STREAM_DONE"); return 1; }

    void *p = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, (off_t)OFF);
    logf("mmap(len=0x%lx, off=0x%lx) = %p %s", LEN, OFF, p, p==MAP_FAILED?strerror(errno):"OK");
    if (p == MAP_FAILED) { close(fd); logf("STREAM_DONE"); return 1; }

    unsigned char *b = (unsigned char*)p;
    /* ① 采样不同位置，看是否是活的图像数据 */
    logf("--- 采样 8 个位置（每处 32 字节）---");
    for (int i = 0; i < 8; i++) {
        unsigned long o = (unsigned long)i * (LEN/8);
        unsigned char *q = b + o;
        int nz = 0; for (int j=0;j<32;j++) if (q[j]) nz++;
        char hx[68]={0}; for(int j=0;j<32;j++) sprintf(hx+j*2,"%02x",q[j]);
        logf("  +0x%08lx nz=%2d/32  %s", o, nz, hx);
    }
    /* ② 全缓冲非零比例（抽样 4096 点） */
    { int nz=0, tot=0;
      for (unsigned long o=0; o<LEN; o+=LEN/4096) { if (b[o]) nz++; tot++; }
      logf("--- 抽样 4096 点：非零 %d/%d (%.1f%%) ---", nz, tot, nz*100.0/tot); }
    /* ③ 唯一字节数（判断是否有丰富的图像内容） */
    { static int seen[256]; memset(seen,0,sizeof seen);
      for (unsigned long o=0; o<LEN; o+=LEN/65536) seen[b[o]]=1;
      int u=0; for(int i=0;i<256;i++) if(seen[i])u++;
      logf("--- 抽样 65536 点：唯一字节值 %d/256 ---", u); }

    munmap(p, LEN); close(fd);
    logf("STREAM_DONE");
    return 0;
}
