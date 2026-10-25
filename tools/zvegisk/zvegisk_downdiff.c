/* zvegisk_downdiff.c — 两次连续 DOWN 的精确 u32 差分（纯读，零注入）
 *
 * 两步分离，便于你看到实时输出：
 *   zvegisk_downdiff <savedir> snap    ← 拍快照保存
 *   [你按 DOWN]
 *   zvegisk_downdiff <savedir> diff    ← 与快照比对，打印变化
 *
 * 快照覆盖：块 #0-#9（前 640KB）+ 块 #37-#41（0x250000~0x2A0000）
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

#define OUT  "/log/zvegisk_dd.log"
#define DEV  "/dev/stream"
#define OFF  0x20312000UL
#define LEN  0x2000000UL
#define CHUNK 0x10000UL

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

/* 要快照的块号 */
static const int BLOCKS[] = {0,1,2,3,4,5,6,7,8,9,37,38,39,40,41};
#define NB (int)(sizeof(BLOCKS)/sizeof(BLOCKS[0]))

static char *snapbuf(unsigned long *sz)
{
    *sz = (unsigned long)NB * CHUNK;
    return malloc(*sz);
}

int main(int argc, char **argv)
{
    wrr();
    const char *dir = (argc>1)?argv[1]:"/log";
    const char *mode = (argc>2)?argv[2]:"snap";
    logf("DOMNDIFF mode=%s dir=%s uid=%d", mode, dir, geteuid());

    int fd = open(DEV, O_RDONLY);
    if (fd < 0) { logf("open FAIL %s", strerror(errno)); return 1; }
    unsigned char *b = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, (off_t)OFF);
    if (b == MAP_FAILED) { logf("mmap FAIL %s", strerror(errno)); close(fd); return 1; }

    unsigned long sz = 0;
    char *buf = snapbuf(&sz);
    if (!buf) { logf("malloc FAIL"); munmap(b,LEN); close(fd); return 1; }

    char path[128]; snprintf(path,sizeof path,"%s/zsnap.bin",dir);

    if (!strcmp(mode,"snap")) {
        unsigned long o = 0;
        for (int i=0;i<NB;i++) { memcpy(buf+o, b + (unsigned long)BLOCKS[i]*CHUNK, CHUNK); o += CHUNK; }
        int f = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (f < 0) { logf("create FAIL %s", strerror(errno)); }
        else { ssize_t w = write(f, buf, sz); close(f); logf("SNAP written %zd bytes to %s", w, path); }
        logf("--- 元数据字段快照 ---");
        for (int i=0;i<NB;i++) {
            unsigned char *p = (unsigned char*)(buf + (unsigned long)i*CHUNK);
            unsigned int v4=0,v8=0,v2c=0; memcpy(&v4,p+0x04,4); memcpy(&v8,p+0x08,4); memcpy(&v2c,p+0x2c,4);
            logf("  块#%2d  +0x04=0x%08x +0x08=0x%08x +0x2c=0x%08x", BLOCKS[i], v4, v8, v2c);
        }
    } else {
        int f = open(path, O_RDONLY);
        if (f < 0) { logf("no snapshot %s (%s)", path, strerror(errno)); munmap(b,LEN); close(fd); return 1; }
        ssize_t r = read(f, buf, sz); close(f);
        if (r != (ssize_t)sz) { logf("snapshot short %zd", r); munmap(b,LEN); close(fd); return 1; }
        logf("已载入快照 %s", path);

        logf("");
        logf("=== 元数据字段对比（QEMU 关心的三个）===");
        for (int i=0;i<NB;i++) {
            unsigned char *a = (unsigned char*)(buf + (unsigned long)i*CHUNK);
            unsigned char *c = (unsigned char*)(b + (unsigned long)BLOCKS[i]*CHUNK);
            unsigned int a4,a8,a2c,c4,c8,c2c;
            memcpy(&a4,a+4,4); memcpy(&a8,a+8,4); memcpy(&a2c,a+0x2c,4);
            memcpy(&c4,c+4,4); memcpy(&c8,c+8,4); memcpy(&c2c,c+0x2c,4);
            if (a4!=c4||a8!=c8||a2c!=c2c)
                logf("  块#%2d  +04 0x%08x->0x%08x   +08 0x%08x->0x%08x   +2c 0x%08x->0x%08x",
                     BLOCKS[i], a4,c4, a8,c8, a2c,c2c);
        }

        logf("");
        logf("=== 全部变化的 u32（最多 100 条）===");
        int cnt = 0;
        for (int i=0;i<NB && cnt<100;i++) {
            unsigned char *a = (unsigned char*)(buf + (unsigned long)i*CHUNK);
            unsigned char *c = (unsigned char*)(b + (unsigned long)BLOCKS[i]*CHUNK);
            for (unsigned long o=0;o+4<=CHUNK && cnt<100;o+=4) {
                unsigned int va,vb; memcpy(&va,a+o,4); memcpy(&vb,c+o,4);
                if (va!=vb) {
                    logf("  块#%2d  +0x%04lx  0x%08x -> 0x%08x  (Δ%d)", BLOCKS[i], o, va, vb, (int)(vb-va));
                    cnt++;
                }
            }
        }
        logf("--- 变化 u32 总数（本快照域内）: %d%s ---", cnt, cnt>=100?"（已截断）":"");
    }

    free(buf); munmap(b, LEN); close(fd);
    logf("DOMNDIFF_DONE");
    return 0;
}
