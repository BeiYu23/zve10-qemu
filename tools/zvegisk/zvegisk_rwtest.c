/* zvegisk_rwtest.c — 试多种 vfat 挂载选项，找出能 R/W 的那个 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

#define OUT "/log/zvegisk_rw.log"
static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(void)
{
    wrr();
    logf("RWTEST_START uid=%d", geteuid());

    int maj=0,min=0; char nm[64]={0};
    { FILE*f=fopen("/proc/partitions","r"); char l[256];
      while(f&&fgets(l,sizeof l,f)){int a,b;unsigned long k;char n[64];
        if(sscanf(l," %d %d %lu %63s",&a,&b,&k,n)==4 && !strncmp(n,"mmca",4)){
          if(!maj || strchr(n,'p')){maj=a;min=b;snprintf(nm,sizeof nm,"%s",n);}
        }} if(f)fclose(f); }
    logf("sd=%s %d:%d",nm,maj,min);
    if(!maj){logf("NO_SD");logf("RWTEST_DONE");return 1;}

    mkdir("/tmp/zdev",0755);
    if(mount("tmpfs","/tmp/zdev","tmpfs",0,0)){logf("tmpfs FAIL %s",strerror(errno));logf("RWTEST_DONE");return 1;}
    char dev[128]; snprintf(dev,sizeof dev,"/tmp/zdev/p1"); unlink(dev);
    if(mknod(dev,S_IFBLK|0666,makedev(maj,min))){logf("mknod FAIL %s",strerror(errno));logf("RWTEST_DONE");return 1;}
    mkdir("/tmp/zsd",0755);

    struct { const char *desc; unsigned long fl; const char *data; } tries[] = {
        {"no-data",            0, 0},
        {"data=rw",            0, "rw"},
        {"data=defaults",      0, "defaults"},
        {"uid/gid",            0, "uid=0,gid=0"},
        {"sync",               0, "sync"},
        {"ro-for-compare",     MS_RDONLY, 0},
    };
    for (int i=0;i<(int)(sizeof(tries)/sizeof(tries[0]));i++){
        int r = mount(dev,"/tmp/zsd","vfat",tries[i].fl,tries[i].data);
        logf("try %-16s rc=%d%s", tries[i].desc, r, r?strerror(errno):" ★ OK");
        if (r==0){
            /* 挂上了，试着写个文件 */
            int fd=open("/tmp/zsd/_zwtest",O_WRONLY|O_CREAT|O_TRUNC,0644);
            if(fd>=0){ int n=write(fd,"ZW\n",3); close(fd); logf("  write test = %d B ★ 可写",n); unlink("/tmp/zsd/_zwtest"); }
            else logf("  write test FAIL: %s",strerror(errno));
            umount("/tmp/zsd");
            break;
        }
    }
    umount("/tmp/zdev");
    logf("RWTEST_DONE");
    return 0;
}
