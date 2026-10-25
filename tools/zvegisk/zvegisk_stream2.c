/* zvegisk_stream2.c — 连续采样 /dev/stream @0x20312000，看它是否随 UI 绘制而变化
 * 只读映射，不写任何东西。
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

#define OUT "/log/zvegisk_stream2.log"
#define DEV "/dev/stream"
#define OFF 0x20312000UL
#define LEN (32UL*1024*1024)

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(void)
{
    wrr();
    logf("STREAM2_START uid=%d", geteuid());
    int fd = open(DEV, O_RDONLY);
    if (fd < 0) { logf("open FAIL %s", strerror(errno)); logf("STREAM2_DONE"); return 1; }
    void *p = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, (off_t)OFF);
    if (p == MAP_FAILED) { logf("mmap FAIL %s", strerror(errno)); close(fd); logf("STREAM2_DONE"); return 1; }
    logf("mapped OK at %p", p);

    unsigned char *b = (unsigned char*)p;
    int best = -1;
    for (int t = 0; t < 20; t++) {
        /* 抽样 8192 点，统计非零 */
        int nz = 0, tot = 0;
        for (unsigned long o = 0; o < LEN; o += LEN/8192) { if (b[o]) nz++; tot++; }
        if (nz > best) best = nz;
        logf("t=%2d 非零 %4d/%d (%.2f%%)", t*5/10, nz, tot, nz*100.0/tot);
        usleep(500000);   /* 0.5 秒 */
    }
    logf("--- 峰值非零: %d ---", best);

    /* 最后dump前 256 字节 */
    { char hx[520]={0}; for(int i=0;i<256;i++) sprintf(hx+i*2,"%02x",b[i]);
      logf("前256字节: %s", hx); }

    munmap(p, LEN); close(fd);
    logf("STREAM2_DONE");
    return 0;
}
