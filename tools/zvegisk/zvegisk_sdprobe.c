/* zvegisk_sdprobe.c — 探测：能不能在相机内部挂载 SD 卡
 *
 * 只读挂载，测完立刻卸载。不写卡、不写任何分区。
 *
 * 关键技巧（绕开 /dev 只读 + 可写分区都是 vfat 的限制）：
 *   tmpfs 挂在 /tmp/zdev → mknod 建设备节点 → mount SD 卡
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

#define OUT  "/log/zvegisk_sd.log"
#define ZDEV "/tmp/zdev"
#define ZSD  "/tmp/zsd"

static void wr_reset(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

/* 从 /proc/partitions 找 SD 卡分区（mmca*），返回 1 找到 */
static int find_sd(int *maj, int *min, char *name, int nlen)
{
    FILE *f = fopen("/proc/partitions", "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        int ma, mi; unsigned long blk; char nm[64];
        if (sscanf(line, " %d %d %lu %63s", &ma, &mi, &blk, nm) == 4) {
            if (strncmp(nm, "mmca", 4) == 0) {
                logf("  PART %s major=%d minor=%d blocks=%lu", nm, ma, mi, blk);
                /* 优先选带 p 的分区（mmca10p1 这种），其次整卡 */
                if (!found || strchr(nm, 'p')) {
                    *maj = ma; *min = mi;
                    snprintf(name, nlen, "%s", nm);
                    found = 1;
                    if (strchr(nm, 'p')) break;
                }
            }
        }
    }
    fclose(f);
    return found;
}

int main(void)
{
    wr_reset();
    logf("SDPROBE_START uid=%d", geteuid());

    int maj = 0, min = 0; char nm[64] = {0};
    logf("--- /proc/partitions 里的 mmca* ---");
    if (!find_sd(&maj, &min, nm, sizeof nm)) {
        logf("NO_SD_PARTITION_FOUND");
        logf("SDPROBE_DONE");
        return 1;
    }
    logf("selected: %s = %d:%d", nm, maj, min);

    /* ① tmpfs */
    mkdir(ZDEV, 0755);
    int r = mount("tmpfs", ZDEV, "tmpfs", 0, 0);
    logf("mount tmpfs rc=%d%s", r, r ? strerror(errno) : "");
    if (r) { logf("SDPROBE_DONE"); return 1; }

    /* ② mknod */
    char dev[128]; snprintf(dev, sizeof dev, "%s/p1", ZDEV);
    unlink(dev);
    r = mknod(dev, S_IFBLK | 0666, makedev(maj, min));
    logf("mknod %s b %d %d rc=%d%s", dev, maj, min, r, r ? strerror(errno) : " OK");
    if (r) { logf("SDPROBE_DONE"); return 1; }

    /* ③ 只读挂载 SD */
    mkdir(ZSD, 0755);
    r = mount(dev, ZSD, "vfat", MS_RDONLY, 0);
    logf("mount vfat ro rc=%d%s", r, r ? strerror(errno) : " OK");
    if (r) {
        /* 试不带 ro 选项 */
        r = mount(dev, ZSD, "vfat", MS_RDONLY, "ro");
        logf("mount vfat ro(data) rc=%d%s", r, r ? strerror(errno) : " OK");
    }
    if (r) { logf("SDPROBE_DONE"); return 1; }

    /* ④ 列目录 */
    DIR *d = opendir(ZSD);
    if (d) {
        struct dirent *e; int n = 0;
        logf("--- SD 卡根目录 ---");
        while ((e = readdir(d)) && n < 20) {
            logf("  %s", e->d_name);
            n++;
        }
        closedir(d);
    } else {
        logf("opendir FAIL: %s", strerror(errno));
    }

    /* ⑤ 尝试读一个已知文件（如果有） */
    {
        char p[160]; snprintf(p, sizeof p, "%s/DCIM", ZSD);
        struct stat st;
        if (stat(p, &st) == 0) logf("DCIM exists, mode=%o", st.st_mode);
        else logf("DCIM stat: %s", strerror(errno));
    }

    /* ⑥ 卸载（只读挂载，卸载是安全的） */
    r = umount(ZSD); logf("umount sd rc=%d%s", r, r?strerror(errno):"");
    r = umount(ZDEV); logf("umount tmpfs rc=%d%s", r, r?strerror(errno):"");

    logf("SDPROBE_DONE");
    return 0;
}
