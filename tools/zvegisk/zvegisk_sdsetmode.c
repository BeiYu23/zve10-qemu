/* zvegisk_sdwrite.c — 在 SD 卡上创建 /zvegisk/ 结构（读写挂载）
 *
 * 只创建目录和一个标记文件，不碰卡上任何已有内容。
 * 完成后验证读回，然后干净卸载。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

#define OUT  "/log/zvegisk_sdw.log"
#define ZDEV "/tmp/zdev"
#define ZSD  "/tmp/zsd"

static void wr_reset(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static int find_sd(int *maj, int *min, char *name, int nlen)
{
    FILE *f = fopen("/proc/partitions","r"); if(!f) return 0;
    char line[256]; int found=0;
    while (fgets(line,sizeof line,f)) {
        int ma,mi; unsigned long blk; char nm[64];
        if (sscanf(line," %d %d %lu %63s",&ma,&mi,&blk,nm)==4 && !strncmp(nm,"mmca",4)) {
            logf("  PART %s %d:%d blocks=%lu", nm, ma, mi, blk);
            if (!found || strchr(nm,'p')) { *maj=ma; *min=mi; snprintf(name,nlen,"%s",nm); found=1; if(strchr(nm,'p')) break; }
        }
    }
    fclose(f); return found;
}

static int write_file(const char *path, const char *data, int len)
{
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { logf("  write %s FAIL: %s", path, strerror(errno)); return -1; }
    int n = write(fd, data, len);
    close(fd);
    logf("  write %s = %d B", path, n);
    return n;
}

int main(void)
{
    wr_reset();
    logf("SDWRITE_START uid=%d", geteuid());

    int maj=0, min=0; char nm[64]={0};
    logf("--- /proc/partitions ---");
    if (!find_sd(&maj,&min,nm,sizeof nm)) { logf("NO_SD"); logf("SDWRITE_DONE"); return 1; }
    logf("selected %s = %d:%d", nm, maj, min);

    mkdir(ZDEV,0755);
    if (mount("tmpfs",ZDEV,"tmpfs",0,0)) { logf("tmpfs FAIL: %s",strerror(errno)); logf("SDWRITE_DONE"); return 1; }

    char dev[128]; snprintf(dev,sizeof dev,"%s/p1",ZDEV); unlink(dev);
    if (mknod(dev,S_IFBLK|0666,makedev(maj,min))) { logf("mknod FAIL: %s",strerror(errno)); logf("SDWRITE_DONE"); return 1; }
    logf("mknod OK");

    mkdir(ZSD,0755);
    /* ★ 读写挂载 */
    int r = mount(dev,ZSD,"vfat",0,0);
    logf("mount rw rc=%d%s", r, r?strerror(errno):" OK");
    if (r) { logf("SDWRITE_DONE"); return 1; }

    /* ① 建目录 */
    char d1[160], d2[200], d3[240];
    snprintf(d1,sizeof d1,"%s/zvegisk",ZSD);
    snprintf(d2,sizeof d2,"%s/zvegisk/mods",ZSD);
    snprintf(d3,sizeof d3,"%s/zvegisk/mods/newmenu",ZSD);
    logf("mkdir %s rc=%d", d1, mkdir(d1,0755));
    logf("mkdir %s rc=%d", d2, mkdir(d2,0755));
    logf("mkdir %s rc=%d", d3, mkdir(d3,0755));

    /* ② 写文件 */
    char p[256];
    snprintf(p,sizeof p,"%s/zvegisk/boot_mode",ZSD);
    write_file(p, "new\n", 4);

    snprintf(p,sizeof p,"%s/zvegisk/README.txt",ZSD);
    const char *readme =
        "zvegisk - ZV-E10 boot mode selector\n"
        "\n"
        "boot_mode : 'old' (default) or 'new'\n"
        "safemode  : if this file exists, nothing is loaded (stock boot)\n"
        "mods/<name>/system/... : files to bind over /usr/...\n"
        "\n"
        "Pulling the SD card always boots stock.\n";
    write_file(p, readme, strlen(readme));

    sync();

    /* ③ 读回验证 */
    logf("--- 读回验证 ---");
    snprintf(p,sizeof p,"%s/zvegisk/boot_mode",ZSD);
    { int fd=open(p,O_RDONLY); char b[64]={0};
      if(fd>=0){int n=read(fd,b,sizeof b-1);close(fd);logf("  boot_mode = %d B: %s",n,b);}
      else logf("  read FAIL: %s",strerror(errno)); }

    /* ④ 列出 /zvegisk */
    snprintf(p,sizeof p,"%s/zvegisk",ZSD);
    DIR *d=opendir(p);
    if (d) { struct dirent *e; logf("--- /zvegisk 内容 ---");
             while((e=readdir(d))) logf("  %s",e->d_name); closedir(d); }

    /* ⑤ 卸载 */
    sync();
    r = umount(ZSD); logf("umount sd rc=%d%s", r, r?strerror(errno):"");
    r = umount(ZDEV); logf("umount tmpfs rc=%d%s", r, r?strerror(errno):"");
    logf("SDWRITE_DONE");
    return 0;
}
