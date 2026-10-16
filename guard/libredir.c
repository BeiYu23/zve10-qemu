/* QEMU 判别性探针：保持 snd_sync_direct 的长度不变，但把"写入目标"改到安全缓冲区
 *
 * 背景：内核 __k_4u_snd_sync_direct 会 copy_to_user(arg+16, …, len-16)，
 *       len 取自 arg[0]（实测 0x910042 ≈ 9.5MB）。
 *       若这段写入落在 App 自己的栈/堆上 ⇒ 会碾坏内存。
 *
 * 本探针：把 ioctl 的第三参数换成一块「16 字节头 + 20MB」的安全 mmap 区，
 *         长度原样保留 ⇒ 内核仍会写 9.5MB，但落在安全区里。
 *
 * 判据：
 *   - 崩溃消失 ⇒ 该写入确实在碾 App 的内存（这就是根因）
 *   - 崩溃仍在 ⇒ 与本路径无关，需另找
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <errno.h>

extern char *__progname;

#define SAFE_SZ (24u * 1024u * 1024u)
static int enabled = 0;
static unsigned char *safe = 0;
static unsigned long n_redir = 0;

static int name_match(const char *n, const char *want)
{
    if (!n) return 0;
    while (*n) { const char *a=n,*b=want; while(*a&&*b&&*a==*b){a++;b++;} if(!*b) return 1; n++; }
    return 0;
}

__attribute__((constructor))
static void r_init(void)
{
    enabled = name_match(__progname, "im.elf") || name_match(__progname, "sen.elf");
    if (!enabled) return;
    safe = mmap(0, SAFE_SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (safe == MAP_FAILED) { safe = 0; enabled = 0; write(2, "[REDIR] mmap failed\n", 20); return; }
    { char m[96]; int k = snprintf(m, sizeof(m), "[REDIR] enabled, safe=%p\n", safe);
      if (k > 0) write(2, m, k); }
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap; void *arg; long r;
    va_start(ap, req); arg = va_arg(ap, void *); va_end(ap);

    if (enabled && safe && req == 0x80000006UL && arg) {
        memcpy(safe, arg, 16);                  /* 头 16 字节原样复制（含长度） */
        n_redir++;
        if (n_redir <= 6) {                     /* 直接打印，崩溃也能看到 */
            char m[128]; int k = snprintf(m, sizeof(m),
                "[REDIR] #%lu len=0x%x -> safe=%p\n", n_redir, *(unsigned int *)safe, safe);
            if (k > 0) write(2, m, k);
        }
        r = syscall(SYS_ioctl, fd, req, safe);  /* ★ 写到安全区 */
        memcpy(arg, safe, 8);                   /* 把前 8 字节结果回写给调用者 */
        return (int)r;
    }
    return (int)syscall(SYS_ioctl, fd, req, arg);
}

__attribute__((destructor))
static void r_report(void)
{
    char m[96]; int k = snprintf(m, sizeof(m), "[REDIR] redirected=%lu\n", n_redir);
    if (k > 0) write(2, m, k);
}
