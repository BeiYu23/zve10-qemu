/* zvegisk_devprobe.c — 探测输入类设备能不能打开/读
 * 安全：只 open + 非阻塞 read，不写任何设备
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#define OUT "/log/zvegisk_dev.log"
static void wr_reset(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

int main(void)
{
    wr_reset();
    logf("DEVPROBE_START uid=%d", geteuid());

    const char *devs[] = {
        "/dev/gpio", "/dev/ioexp", "/dev/uinput",
        "/dev/input/event0", "/dev/input/event1", "/dev/input/event2",
        "/dev/stream", "/dev/stream2", "/dev/ldec", "/dev/dmpgles2",
        "/dev/dmac", "/dev/emif", "/dev/boss-t1",
        0
    };

    for (int i = 0; devs[i]; i++) {
        /* 每次 fork 隔离，一个设备炸了不影响其它 */
        pid_t p = fork();
        if (p == 0) {
            int fd = open(devs[i], O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                logf("%-22s open=FAIL errno=%d", devs[i], errno);
                _exit(0);
            }
            /* 试着非阻塞读一点点 */
            char buf[64];
            int n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                char hx[3*32+1]; int k=0;
                for (int j=0;j<n && j<16;j++){ sprintf(hx+k,"%02x",(unsigned char)buf[j]); k+=2; }
                hx[k]=0;
                logf("%-22s open=OK read=%d data=%s", devs[i], n, hx);
            } else {
                logf("%-22s open=OK read=%d errno=%d", devs[i], n, n<0?errno:0);
            }
            close(fd);
            _exit(0);
        }
        int st=0; waitpid(p,&st,0);
        if (WIFSIGNALED(st))
            logf("%-22s CRASH sig=%d", devs[i], WTERMSIG(st));
    }

    /* 再确认一下 input.ko 加载后的 proc 视图 */
    {
        int fd = open("/proc/bus/input/devices", O_RDONLY);
        logf("proc/bus/input/devices open=%d", fd);
        if (fd >= 0) { close(fd); }
    }
    logf("DEVPROBE_DONE");
    return 0;
}
