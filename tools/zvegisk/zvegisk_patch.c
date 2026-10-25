/* zvegisk_patch.c — 内存补丁工具（可打可恢复）
 *
 * 用法：
 *   zvegisk_patch <pid> save     只保存原始字节到 /log/zvegisk_orig.bin
 *   zvegisk_patch <pid> apply    打补丁（GRM_gpermFillrect → 直接 return 0）
 *   zvegisk_patch <pid> restore  从 /log/zvegisk_orig.bin 恢复
 *   zvegisk_patch <pid> test     保存 → 打补丁 → 等 3 秒 → 恢复（推荐）
 *
 * 补丁内容：在 GRM_gpermFillrect 入口写 `00 20 70 47`（Thumb: movs r0,#0; bx lr）
 *           ⇒ 函数立刻返回 GRM_SUCCESS，不绘制任何东西
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>

#define OUT    "/log/zvegisk_patch.log"
#define ORIG   "/log/zvegisk_orig.bin"
#define FILLRECT_VADDR 0x683f4cUL
#define PATCH_LEN 4

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static unsigned long find_base(int pid,const char*so,int*f){
    char p[64]; snprintf(p,sizeof p,"/proc/%d/maps",pid);
    FILE*fp=fopen(p,"r"); *f=0; if(!fp)return 0;
    char l[512];
    while(fgets(l,sizeof l,fp)){
        if(!strstr(l,so)||!strstr(l,"r-xp"))continue;
        unsigned long s=0;
        if(sscanf(l,"%lx-%*lx",&s)==1){*f=1;fclose(fp);return s;}
    }
    fclose(fp);return 0;
}

/* 保存原始字节 */
static int save_orig(int fd, unsigned long addr)
{
    unsigned char b[PATCH_LEN];
    int n = pread(fd, b, PATCH_LEN, (off_t)addr);
    if (n != PATCH_LEN) { logf("save: pread=%d (%s)", n, strerror(errno)); return -1; }
    int f = open(ORIG, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (f < 0) { logf("save: open %s FAIL %s", ORIG, strerror(errno)); return -1; }
    /* 文件格式: [8字节地址][4字节原始数据] */
    unsigned char rec[12];
    memcpy(rec, &addr, 8);
    memcpy(rec+8, b, 4);
    int w = write(f, rec, 12); close(f);
    logf("save: addr=0x%lx bytes=%02x %02x %02x %02x (wrote %d)",
         addr, b[0],b[1],b[2],b[3], w);
    return 0;
}

/* 打补丁 */
static int apply(int fd, unsigned long addr)
{
    /* Thumb: movs r0,#0 ; bx lr  =  00 20 70 47 */
    unsigned char p[PATCH_LEN] = {0x00, 0x20, 0x70, 0x47};
    ssize_t w = pwrite(fd, p, PATCH_LEN, (off_t)addr);
    logf("apply: pwrite=%zd%s", w, w<0?strerror(errno):"");
    if (w != PATCH_LEN) return -1;
    unsigned char chk[PATCH_LEN];
    pread(fd, chk, PATCH_LEN, (off_t)addr);
    logf("apply: verify = %02x %02x %02x %02x", chk[0],chk[1],chk[2],chk[3]);
    return 0;
}

/* 从文件恢复 */
static int restore(int fd)
{
    unsigned char rec[12];
    int f = open(ORIG, O_RDONLY);
    if (f < 0) { logf("restore: no %s (%s)", ORIG, strerror(errno)); return -1; }
    int n = read(f, rec, 12); close(f);
    if (n != 12) { logf("restore: short read %d", n); return -1; }
    unsigned long addr = 0;
    memcpy(&addr, rec, 8);
    ssize_t w = pwrite(fd, rec+8, 4, (off_t)addr);
    logf("restore: addr=0x%lx pwrite=%zd%s bytes=%02x %02x %02x %02x",
         addr, w, w<0?strerror(errno):"", rec[8],rec[9],rec[10],rec[11]);
    if (w != 4) return -1;
    unsigned char chk[4];
    pread(fd, chk, 4, (off_t)addr);
    logf("restore: verify = %02x %02x %02x %02x  %s",
         chk[0],chk[1],chk[2],chk[3],
         (memcmp(chk,rec+8,4)==0)?"MATCH ★":"MISMATCH !!");
    return 0;
}

int main(int argc, char **argv)
{
    wrr();
    logf("PATCH_START uid=%d", geteuid());
    int pid = (argc > 1) ? atoi(argv[1]) : 157;
    const char *mode = (argc > 2) ? argv[2] : "test";
    logf("pid=%d mode=%s", pid, mode);

    int found = 0;
    unsigned long base = find_base(pid, "libObj.so", &found);
    if (!found) { logf("NO_BASE"); logf("PATCH_DONE"); return 1; }
    unsigned long addr = base + FILLRECT_VADDR;
    logf("libObj base=0x%lx  target=0x%lx", base, addr);

    char mp[64]; snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    logf("open %s = %d%s", mp, fd, fd<0?strerror(errno):"");
    if (fd < 0) { logf("PATCH_DONE"); return 1; }

    if (!strcmp(mode,"save"))        { save_orig(fd, addr); }
    else if (!strcmp(mode,"apply"))  { save_orig(fd, addr); apply(fd, addr); }
    else if (!strcmp(mode,"restore")){ restore(fd); }
    else { /* test */
        if (save_orig(fd, addr) == 0) {
            if (apply(fd, addr) == 0) {
                logf("--- patched, holding 3 s (watch the screen!) ---");
                sync();
                sleep(3);
                logf("--- restoring ---");
                restore(fd);
            }
        }
    }

    close(fd);
    logf("PATCH_DONE");
    return 0;
}
