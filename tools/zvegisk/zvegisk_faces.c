/* zvegisk_faces.c — 找出真正的像素面
 *
 * 对每个候选映射做：只读 mmap + 结构分析 + RGB565 解码统计
 * 然后连续采样时间序列，检测哪些字段在 UI 变化时改变。
 *
 * ★ 全程只读，不写任何设备/内存。
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

#define OUT "/log/zvegisk_faces.log"

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

struct face { const char *dev; unsigned long off; unsigned long len; const char *name; };

static struct face faces[] = {
    { "/dev/stream", 0x20312000UL, 0x2000000UL, "stream@20312000 (32MB)" },
    { "/dev/stream", 0x22312000UL, 0x400000UL,  "stream@22312000 (4MB)"  },
    { "/dev/stream", 0x3f7ed000UL, 0x800000UL,  "stream@3f7ed000 (8MB)"  },
    { "/dev/mem",    0x3f3ec000UL, 0x400000UL,  "mem@3f3ec000 (4MB)"     },
    { "/dev/mem",    0x80000000UL, 0x800000UL,  "mem@80000000 (8MB,wo)"  },
};
#define NF (int)(sizeof(faces)/sizeof(faces[0]))

static void *maps[8];
static int   fds[8];

/* RGB565 统计：唯一色数、行间方差、非零比例 */
static void rgb565_stats(unsigned char *b, unsigned long len, int *uniq, double *rowvar, double *nzr)
{
    /* 按 640x480 RGB565 = 614400 B 采样 */
    #define W 640
    #define H 480
    #define BPP 2
    unsigned long need = (unsigned long)W*H*BPP;
    if (len < need) need = len;

    /* 唯一色（抽样 4096 像素） */
    static unsigned char seen[65536];
    memset(seen, 0, sizeof seen);
    unsigned long step = (need/BPP)/4096; if (step < 1) step = 1;
    int u = 0, nzp = 0, tot = 0;
    for (unsigned long px = 0; px < need/BPP; px += step) {
        unsigned v = b[px*2] | (b[px*2+1] << 8);
        if (!seen[v]) { seen[v] = 1; u++; }
        if (v) nzp++;
        tot++;
    }
    *uniq = u;
    *nzr = tot ? (double)nzp/tot : 0;

    /* 行间差异：比较相邻两行的平均绝对差（抽样 100 列 x 20 行对） */
    double sum = 0; int cnt = 0;
    for (int y = 0; y < H-1 && (unsigned long)(y+1)*W*BPP < need; y += 20) {
        for (int x = 0; x < W; x += 16) {
            int a = b[(y*W + x)*BPP], c = b[((y+1)*W + x)*BPP];
            int d = a - c; if (d < 0) d = -d;
            sum += d; cnt++;
        }
    }
    *rowvar = cnt ? sum/cnt : 0;
}

/* 计算一个面的指纹（用于时间序列比较） */
static void fingerprint(unsigned char *b, unsigned long len, unsigned long *sum, int *nz, unsigned long *tail)
{
    unsigned long s = 0; int n = 0;
    unsigned long step = len/4096; if (!step) step = 64;
    for (unsigned long o = 0; o < len; o += step) { s = s*31 + b[o]; if (b[o]) n++; }
    *sum = s; *nz = n;
    /* 尾部指纹：有些环的写指针在末尾 */
    unsigned long t = 0;
    for (unsigned long o = len > 256 ? len-256 : 0; o < len; o++) t = t*17 + b[o];
    *tail = t;
}

int main(void)
{
    wrr();
    logf("FACES_START uid=%d", geteuid());

    /* ① 逐个映射 + 静态分析 */
    for (int i = 0; i < NF; i++) {
        logf("");
        logf("=== %s ===", faces[i].name);
        int fd = open(faces[i].dev, O_RDONLY);
        if (fd < 0) { logf("  open FAIL: %s", strerror(errno)); fds[i] = -1; continue; }
        void *p = mmap(NULL, faces[i].len, PROT_READ, MAP_SHARED, fd, (off_t)faces[i].off);
        if (p == MAP_FAILED) {
            logf("  mmap(PROT_READ) FAIL: %s", strerror(errno));
            close(fd); fds[i] = -1; continue;
        }
        fds[i] = fd; maps[i] = p;
        unsigned char *b = (unsigned char*)p;

        /* 前 64 字节 */
        { char hx[132]={0}; for(int j=0;j<64;j++) sprintf(hx+j*2,"%02x",b[j]);
          logf("  head64: %s", hx); }

        int uniq; double rv, nz;
        rgb565_stats(b, faces[i].len, &uniq, &rv, &nz);
        logf("  RGB565@640x480: 唯一色 %d, 行间差 %.2f, 非零 %.1f%%", uniq, rv, nz*100.0);
    }

    /* ② sysfs 引用计数（进菜单前） */
    logf("");
    logf("=== sysfs refcnt (before menu nav) ===");
    { const char *paths[] = {"/sys/module/grm_ma/refcnt","/sys/module/dmm/refcnt",
                             "/sys/module/grm_gles/refcnt","/sys/module/liro/refcnt",0};
      for(int i=0;paths[i];i++){ int f=open(paths[i],O_RDONLY); char lb[64]={0};
        if(f>=0){int n=read(f,lb,sizeof lb-1); close(f); logf("  %s = %s", paths[i], n>0?lb:"?");}
        else logf("  %s = ERR %s", paths[i], strerror(errno)); } }

    /* ③ ★ 时间序列：100 轮 × 100ms，期间用户按 DOWN */
    logf("");
    logf("=== 时间序列（100 轮 x 100ms，请期间按 DOWN 移动选中项）===");
    {
        unsigned long prev_sum[8]={0}; int prev_nz[8]={0}; unsigned long prev_tail[8]={0};
        int changed[8]={0};
        for (int round = 0; round < 100; round++) {
            for (int i = 0; i < NF; i++) {
                if (!maps[i]) continue;
                unsigned long s, t; int n;
                fingerprint((unsigned char*)maps[i], faces[i].len, &s, &n, &t);
                if (round > 0 && (s != prev_sum[i] || n != prev_nz[i] || t != prev_tail[i])) {
                    if (changed[i] < 5)
                        logf("  t=%3d %-24s CHANGED  sum %lx->%lx  nz %d->%d  tail %lx->%lx",
                             round, faces[i].name, prev_sum[i], s, prev_nz[i], n, prev_tail[i], t);
                    changed[i]++;
                }
                prev_sum[i]=s; prev_nz[i]=n; prev_tail[i]=t;
            }
            usleep(100000);
        }
        logf("--- 变化次数统计 ---");
        for (int i = 0; i < NF; i++)
            if (maps[i]) logf("  %-24s 变化 %d 次", faces[i].name, changed[i]);
    }

    /* ④ sysfs refcnt（进菜单后） */
    logf("");
    logf("=== sysfs refcnt (after menu nav) ===");
    { const char *paths[] = {"/sys/module/grm_ma/refcnt","/sys/module/dmm/refcnt",
                             "/sys/module/grm_gles/refcnt","/sys/module/liro/refcnt",0};
      for(int i=0;paths[i];i++){ int f=open(paths[i],O_RDONLY); char lb[64]={0};
        if(f>=0){int n=read(f,lb,sizeof lb-1); close(f); logf("  %s = %s", paths[i], n>0?lb:"?");}
        else logf("  %s = ERR %s", paths[i], strerror(errno)); } }

    for (int i=0;i<NF;i++) if (maps[i]) munmap(maps[i], faces[i].len);
    for (int i=0;i<NF;i++) if (fds[i]>=0) close(fds[i]);
    logf("");
    logf("FACES_DONE");
    return 0;
}
