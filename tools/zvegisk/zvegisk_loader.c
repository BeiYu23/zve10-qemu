#define _GNU_SOURCE
/* zvegisk_loader.c — 独立加载器（可执行文件，不是 .so）
 *
 * 由 agent 的 RUN 拉起。设计目标：
 *   ★ 加载损坏的 .so 也【不会】拖垮 agent —— 崩溃被限制在自己的子进程里
 *
 * 流程：
 *   父进程 fork
 *     ├─ 子进程：dlopen + dlsym（所有危险操作都在这里）
 *     └─ 父进程：waitpid，记录子进程退出状态（即使崩了也能记下）
 *
 * 输出：/log/zvegisk_probe.log
 * 编译：静态链接，避免相机 glibc 2.11.2 的符号版本问题
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <signal.h>

#define OUT "/log/zvegisk_probe.log"
#define LIBOBJ "/usr/lib/libObj.so"

/* 截断（只在父进程 fork 之前调用一次） */
static void wr_reset(void)
{
    int fd = open(OUT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

/* ★ 追加写 —— 父子的输出不会互相覆盖 */
static void wr(const char *s)
{
    int fd = open(OUT, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}

/* 子进程：所有危险操作 */
static void probe(void)
{
    char buf[4096];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "ZVEGISK_LOADER_START\n");
    n += snprintf(buf + n, sizeof(buf) - n, "pid=%d uid=%d\n", getpid(), geteuid());

    /* ① 先看当前进程能不能解析到 libObj 里的符号
     *    （如果 agent 进程本身已加载 libObj，RTLD_DEFAULT 就能找到） */
    void *sym = dlsym(RTLD_DEFAULT, "GRM_gpermFillrect");
    n += snprintf(buf + n, sizeof(buf) - n, "dlsym(RTLD_DEFAULT, GRM_gpermFillrect)=%p\n", sym);

    /* ② 尝试自己 dlopen libObj.so */
    void *h = dlopen(LIBOBJ, RTLD_LAZY | RTLD_GLOBAL);
    n += snprintf(buf + n, sizeof(buf) - n, "dlopen(%s)=%p\n", LIBOBJ, h);
    if (!h) {
        n += snprintf(buf + n, sizeof(buf) - n, "dlerror=%s\n", dlerror());
        /* libObj 加载失败不代表整体失败 —— 可能已经由宿主进程加载 */
    }

    /* ③ 逐个 dlsym 那七个绘图符号 */
    const char *names[] = {
        "GRM_screenGetOnBitmap",
        "GRM_gpermFillrect",
        "GRM_gpermRectblit",
        "GRM_bitmapCreate",
        "GRM_bitmapWriteLockBits",
        "GRM_bitmapWriteUnlockBits",
        "GRM_bitmapGetPhysicalAddress",
        0
    };
    for (int i = 0; names[i]; i++) {
        void *p1 = h ? dlsym(h, names[i]) : 0;
        void *p2 = dlsym(RTLD_DEFAULT, names[i]);
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%-28s libobj=%p default=%p\n", names[i], p1, p2);
    }

    /* ④ ★ 只取地址，不调用任何 GRM 函数 */
    n += snprintf(buf + n, sizeof(buf) - n, "PROBE_DONE (no GRM function was called)\n");
    wr(buf);
    _exit(0);
}

int main(void)
{
    signal(SIGSEGV, SIG_DFL);
    wr_reset();               /* 先清空，之后全部追加 */
    pid_t p = fork();
    if (p == 0) {
        probe();          /* 不会返回；崩了也只有这个子进程死 */
        _exit(1);
    }
    if (p < 0) {
        wr("FORK_FAILED\n");
        return 1;
    }
    int st = 0;
    waitpid(p, &st, 0);

    /* 父进程记录子进程结局 —— 即使子进程段错误，这里也能写下 */
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "CHILD_STATUS=0x%08x ", st);
    if (WIFSIGNALED(st))
        n += snprintf(buf + n, sizeof(buf) - n, "KILLED_BY_SIGNAL=%d (SIGSEGV=%d)\n",
                      WTERMSIG(st), SIGSEGV);
    else
        n += snprintf(buf + n, sizeof(buf) - n, "EXIT_CODE=%d\n", WEXITSTATUS(st));
    wr(buf);
    return WIFSIGNALED(st) ? 2 : 0;
}
