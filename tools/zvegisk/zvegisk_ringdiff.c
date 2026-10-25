/* zvegisk_ringdiff.c — 对 /dev/stream @0x20312000 命令环做前后字节差分
 *
 * 三次快照（A → 按 DOWN → B → 按 DOWN → C），输出 A→B 和 B→C 的精确差分。
 * ★ 全程只读。
 *
 * 用法: zvegisk_ringdiff [扫描长度, 默认 0x400000 = 4MB]
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

#define OUT   "/log/zvegisk_ringdiff.log"
#define DEV   "/dev/stream"
#define OFF   0x20312000UL
#define MAXDIFF 200

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static void diff(const char *name, unsigned char *a, unsigned char *b, unsigned long len)
{
    logf("");
    logf("=== DIFF %s (len=0x%lx) ===", name, len);
    int cnt = 0;
    /* 先按 4 字节比较，报出所有变化的 u32 */
    for (unsigned long o = 0; o + 4 <= len; o += 4) {
        unsigned int va, vb;
        memcpy(&va, a+o, 4); memcpy(&vb, b+o, 4);
        if (va != vb) {
            if (cnt < MAXDIFF)
                logf("  off=0x%08lx  0x%08x -> 0x%08x", o, va, vb);
            else if (cnt == MAXDIFF)
                logf("  ... (还有更多，已截断)");
            cnt++;
        }
    }
    logf("--- 变化 u32 总数: %d ---", cnt);
    /* 偏移间距分析（判断槽位步长） */
    if (cnt > 1 && cnt <= 64) {
        unsigned long prev = 0; int first = 1;
        logf("--- 变化偏移间距 ---");
        for (unsigned long o = 0; o + 4 <= len && cnt <= 64; o += 4) {
            unsigned int va, vb;
            memcpy(&va, a+o, 4); memcpy(&vb, b+o, 4);
            if (va != vb) {
                if (!first && o - prev != 4) logf("    间距 %lu (0x%lx -> 0x%lx)", o-prev, prev, o);
                prev = o; first = 0;
            }
        }
    }
}

int main(int argc, char **argv)
{
    wrr();
    logf("RINGDIFF_START uid=%d", geteuid());
    unsigned long scan = (argc > 1) ? strtoul(argv[1], 0, 0) : 0x400000UL;
    logf("scan length = 0x%lx (%lu KB)", scan, scan/1024);

    int fd = open(DEV, O_RDONLY);
    if (fd < 0) { logf("open FAIL %s", strerror(errno)); logf("RINGDIFF_DONE"); return 1; }
    void *p = mmap(NULL, 0x2000000UL, PROT_READ, MAP_SHARED, fd, (off_t)OFF);
    if (p == MAP_FAILED) { logf("mmap FAIL %s", strerror(errno)); close(fd); logf("RINGDIFF_DONE"); return 1; }
    logf("mapped at %p", p);

    unsigned char *a = malloc(scan), *b = malloc(scan), *c = malloc(scan);
    if (!a||!b||!c) { logf("malloc FAIL"); munmap(p,0x2000000UL); close(fd); logf("RINGDIFF_DONE"); return 1; }

    logf(">>> 快照 A（请保持菜单静止 3 秒）");
    memcpy(a, p, scan);
    sync(); sleep(3);

    logf(">>> 快照 B —— 请现在按一次 DOWN");
    memcpy(b, p, scan);
    sync(); sleep(3);

    logf(">>> 快照 C —— 请再按一次 DOWN");
    memcpy(c, p, scan);
    sync(); sleep(3);

    diff("A->B (第一次 DOWN)", a, b, scan);
    diff("B->C (第二次 DOWN)", b, c, scan);

    /* 也给出 A 与 C 的差异总数（跨两次 DOWN） */
    { int n=0; for(unsigned long o=0;o+4<=scan;o+=4){ unsigned int x,y; memcpy(&x,a+o,4); memcpy(&y,c+o,4); if(x!=y)n++; }
      logf("");
      logf("=== A->C 变化 u32 总数: %d ===", n); }

    free(a); free(b); free(c);
    munmap(p, 0x2000000UL); close(fd);
    logf("RINGDIFF_DONE");
    return 0;
}
