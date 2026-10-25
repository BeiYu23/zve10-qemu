/* zvegisk_ringdiff2.c — 整数 32MB 分块哈希定位 + 精确差分
 *
 * 流程：
 *   ① 分块哈希（每 64KB 一块）基线
 *   ② 提示按 DOWN（等 6 秒）
 *   ③ 再次分块哈希 → 找出哪些块变了
 *   ④ 对被改变的块做精确 u32 差分
 *   ⑤ 再来一轮（第二次 DOWN）
 * ★ 全程只读
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

#define OUT  "/log/zvegisk_rd2.log"
#define DEV  "/dev/stream"
#define OFF  0x20312000UL
#define LEN  0x2000000UL          /* 32MB */
#define CHUNK 0x10000UL           /* 64KB */
#define NCH  (LEN/CHUNK)          /* 512 块 */

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static unsigned long hashes[NCH];

static void hash_all(unsigned char *b)
{
    for (unsigned long i = 0; i < NCH; i++) {
        unsigned long h = 5381;
        unsigned char *p = b + i*CHUNK;
        for (unsigned long j = 0; j < CHUNK; j += 4) h = h*33 + p[j] + (p[j+1]<<8) + (p[j+2]<<16) + (p[j+3]<<24);
        hashes[i] = h;
    }
}

int main(void)
{
    wrr();
    logf("RD2_START uid=%d", geteuid());
    int fd = open(DEV, O_RDONLY);
    if (fd < 0) { logf("open FAIL %s", strerror(errno)); logf("RD2_DONE"); return 1; }
    unsigned char *b = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, (off_t)OFF);
    if (b == MAP_FAILED) { logf("mmap FAIL %s", strerror(errno)); close(fd); logf("RD2_DONE"); return 1; }
    logf("mapped 32MB at %p", b);

    static unsigned long prev[NCH], cur[NCH];
    unsigned char *snapA = malloc(CHUNK), *snapB = malloc(CHUNK);
    if(!snapA||!snapB){logf("malloc FAIL");munmap(b,LEN);close(fd);logf("RD2_DONE");return 1;}

    /* 基线 */
    logf("");
    logf(">>> 基线哈希（全 32MB，512 块）");
    hash_all(b);
    memcpy(prev, hashes, sizeof hashes);

    for (int round = 1; round <= 2; round++) {
        logf("");
        logf("=====================================================");
        logf(">>> 第 %d 次：请【现在按一次 DOWN 键】，等 6 秒", round);
        logf("=====================================================");
        sync();
        sleep(6);

        hash_all(b);
        memcpy(cur, hashes, sizeof hashes);

        int nchg = 0;
        logf("--- 变化的 64KB 块 ---");
        for (int i = 0; i < (int)NCH; i++) {
            if (cur[i] != prev[i]) {
                logf("  块 #%d  off=0x%08lx  hash %lx -> %lx", i, (unsigned long)i*CHUNK, prev[i], cur[i]);
                nchg++;
                if (nchg > 20) { logf("  ...(更多已截断)"); break; }
            }
        }
        logf("--- 变化块数: %d ---", nchg);
        memcpy(prev, cur, sizeof cur);
    }

    free(snapA); free(snapB);
    munmap(b, LEN); close(fd);
    logf("");
    logf("RD2_DONE");
    return 0;
}
