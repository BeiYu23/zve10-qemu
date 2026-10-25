/* zvegisk_loader2.c — Phase 2：确认哪些 API 是空桩，哪些是真的
 *
 * 安全策略：
 *   ★ 只调用【无参】函数（r0-r3 全清零），即使原型猜错也不会读到野指针
 *   ★ 每个调用单独 fork，一个崩了不影响下一个
 *   ★ 父进程统一记录结果
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

#define OUT "/log/zvegisk_probe2.log"
#define LIBOBJ "/usr/lib/libObj.so"

static void wr_reset(void){ int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0) close(fd); }
static void wr(const char *s){ int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,s,strlen(s));close(fd);} }
static void logf(const char *fmt, ...){
    char b[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    strncat(b,"\n",sizeof(b)-strlen(b)-1);
    wr(b);
}

/* 在一个隔离子进程里调用无参函数，返回它的 r0 */
static unsigned call_nop(int (*fn)(void))
{
    pid_t p = fork();
    if (p == 0) {
        /* 子进程；故意清零 r0-r3 由编译器保证无参调用 */
        unsigned r = (unsigned)fn();
        _exit((int)(r & 0xFF));   /* 只能带 8 位出来，够区分 0/非0 */
    }
    int st = 0;
    waitpid(p, &st, 0);
    if (WIFSIGNALED(st)) return 0xDEAD0000u | (unsigned)WTERMSIG(st);
    return (unsigned)WEXITSTATUS(st);
}

int main(void)
{
    wr_reset();
    void *h = dlopen(LIBOBJ, RTLD_LAZY | RTLD_GLOBAL);
    logf("dlopen(%s) = %p", LIBOBJ, h);
    if (!h) { logf("dlerror=%s", dlerror()); return 1; }
    logf("uid=%d", geteuid());

    /* ① 无参空桩：调用它们完全安全 */
    struct { const char *name; } nop[] = {
        {"GRM_screenGetOnBitmap"},
        {"GRM_fbrmInitializeYUV"},
        {"GRM_fbrmPrepareUpdateYUV"},
        {"GRM_fbrmUpdateYUV"},
        {"GRM_fbrmTerminateYUV"},
    };
    logf("--- 无参函数调用结果（0 = 空桩/返回NULL）---");
    for (int i = 0; i < (int)(sizeof(nop)/sizeof(nop[0])); i++) {
        void *p = dlsym(h, nop[i].name);
        if (!p) { logf("%-28s = (dlsym FAIL)", nop[i].name); continue; }
        unsigned r = call_nop((int (*)(void))p);
        if (r & 0xDEAD0000u)
            logf("%-28s = CRASH sig=%u", nop[i].name, r & 0xFFFFu);
        else
            logf("%-28s = 0x%02x%s", nop[i].name, r, r ? "" : "  ← 空桩");
    }

    /* ② 只取地址的真实函数（不调用 —— 参数复杂） */
    logf("--- 只取地址（不调用）---");
    const char *addr_only[] = {
        "GRM_gpermFillrect", "GRM_gpermRectblit",
        "GRM_bitmapCreate", "BMP_Create",
        "GRM_bitmapCreateComposite", "GRM_fbrmSetScreenPreference",
        "GRM_bitmapWriteLockBits", "GRM_bitmapWriteUnlockBits",
        "GRM_bitmapGetPhysicalAddress", "GRM_bitmapGetSubRectangle",
        "GRM_bitmapGetRowPixelCount", 0
    };
    for (int i = 0; addr_only[i]; i++)
        logf("%-30s = %p", addr_only[i], dlsym(h, addr_only[i]));

    logf("PROBE2_DONE (no complex-arg function was called)");
    return 0;
}
