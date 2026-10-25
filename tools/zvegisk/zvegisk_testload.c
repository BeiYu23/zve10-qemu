/* zvegisk_testload.c — 在隔离子进程里 dlopen zvegisk.so 并观察其行为
 *
 * ★ 崩溃隔离：所有危险操作在子进程；父进程只记录结局。
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

#define OUT "/log/zvegisk_test.log"
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}

int main(int argc, char **argv)
{
    const char *path = (argc>1)?argv[1]:"/log/zvegisk.so";
    wrr();
    char b[256]; snprintf(b,sizeof b,"TESTLOAD start path=%s uid=%d\n",path,geteuid()); wr(b);

    pid_t p = fork();
    if (p == 0) {
        void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        snprintf(b,sizeof b,"child: dlopen=%p\n", h);
        wr(b);
        if (!h) { snprintf(b,sizeof b,"child: dlerror=%s\n", dlerror()); wr(b); _exit(2); }
        /* 构造函数的内部 fork 需要时间：等 6 秒让它挂 SD / 读 boot_mode / 写日志 */
        sleep(6);
        _exit(0);
    }
    int st=0; waitpid(p,&st,0);
    if (WIFSIGNALED(st)) snprintf(b,sizeof b,"PARENT: child KILLED sig=%d\n",WTERMSIG(st));
    else snprintf(b,sizeof b,"PARENT: child exit=%d\n",WEXITSTATUS(st));
    wr(b);
    return 0;
}
