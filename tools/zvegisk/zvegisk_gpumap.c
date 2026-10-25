/* zvegisk_gpumap.c — mmap /dev/dmpgles2 看看映射到什么
 * 只读映射，不写。三种尺寸都试（512 / 128KB / 32MB）。
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

#define OUT "/log/zvegisk_gpu.log"
static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(void)
{
    wrr();
    logf("GPUMAP_START uid=%d", geteuid());
    int fd = open("/dev/dmpgles2", O_RDONLY);
    logf("open(/dev/dmpgles2) O_RDONLY = %d %s", fd, fd<0?strerror(errno):"");
    if (fd < 0) { logf("GPUMAP_DONE"); return 1; }

    struct { unsigned long sz; const char *nm; } tries[] = {
        {0x200UL,     "512B"},
        {0x20000UL,   "128KB"},
        {0x2000000UL, "32MB"},
    };
    for (int i=0;i<3;i++) {
        logf("");
        logf("=== mmap size=0x%lx (%s) offset=0 ===", tries[i].sz, tries[i].nm);
        void *p = mmap(NULL, tries[i].sz, PROT_READ, MAP_SHARED, fd, 0);
        logf("  mmap = %p %s", p, p==MAP_FAILED?strerror(errno):"OK");
        if (p == MAP_FAILED) continue;
        unsigned char *b=(unsigned char*)p;
        /* 头 64 字节 */
        { char hx[132]={0}; for(int j=0;j<64;j++) sprintf(hx+j*2,"%02x",b[j]);
          logf("  head64: %s", hx); }
        /* 统计 */
        int nz=0, tot=0; static int seen[256]; memset(seen,0,sizeof seen); int u=0;
        unsigned long step = tries[i].sz/4096; if(!step) step=1;
        for (unsigned long o=0;o<tries[i].sz;o+=step) {
            if (b[o]) nz++;
            if(!seen[b[o]]){seen[b[o]]=1;u++;}
            tot++;
        }
        logf("  非零 %d/%d (%.1f%%)  唯一字节 %d/256", nz, tot, tot?nz*100.0/tot:0, u);
        munmap(p, tries[i].sz);
    }
    /* 也试 O_RDWR 打开看能不能写映射 */
    close(fd);
    logf("");
    { int fd2=open("/dev/dmpgles2",O_RDWR);
      logf("open O_RDWR = %d %s", fd2, fd2<0?strerror(errno):"");
      if(fd2>=0){
          void *p=mmap(NULL,0x2000000UL,PROT_READ|PROT_WRITE,MAP_SHARED,fd2,0);
          logf("mmap RW 32MB = %p %s", p, p==MAP_FAILED?strerror(errno):"★ 可写映射成功");
          if(p!=MAP_FAILED) munmap(p,0x2000000UL);
          close(fd2);
      } }
    logf("GPUMAP_DONE");
    return 0;
}
