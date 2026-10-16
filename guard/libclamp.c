/* QEMU 探针：夹住 snd_sync_direct(0x80000006) 的过大长度
 *
 * 背景：内核 __k_4u_snd_sync_direct 把 arg[0] 当"总长度"，
 *       然后 copy_to_user(arg+16, …, len-16)。
 *       App 实测传入 0x910042(≈9.5MB) ⇒ 内核写穿 App 内存 ⇒ 崩。
 *
 * 本探针只改 ioctl 的入参（用一份改过的副本），用于**确认根因**：
 *   若夹住长度后崩溃消失 ⇒ 根因 100% 确认。
 *
 * 注意：这是**语义探针**，不是最终修复。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <errno.h>

extern char *__progname;

#define CLAMP_MAX 0x1000u          /* 把长度夹到 4KB 以内 */

static int enabled = 0;
static unsigned long n_clamped = 0;

static int name_match(const char *n, const char *want)
{
    if (!n) return 0;
    while (*n) { const char *a=n,*b=want; while(*a&&*b&&*a==*b){a++;b++;} if(!*b) return 1; n++; }
    return 0;
}

__attribute__((constructor))
static void c_init(void)
{
    enabled = name_match(__progname, "im.elf") || name_match(__progname, "sen.elf");
    if (enabled) write(2, "[CLAMP] enabled\n", 16);
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap; void *arg; long r;
    va_start(ap, req); arg = va_arg(ap, void *); va_end(ap);

    if (enabled && req == 0x80000006UL && arg) {
        unsigned int hdr[4];
        /* 只读 8 字节足够（用户态只放了 8 字节头） */
        memcpy(hdr, arg, sizeof(hdr) < 16 ? sizeof(hdr) : 16);
        if (hdr[0] > CLAMP_MAX) {
            unsigned char buf[16];
            unsigned int old = hdr[0];
            memcpy(buf, arg, 16);
            *(unsigned int *)buf = 16u;          /* 总长度改成 16 ⇒ 不写回数据 */
            n_clamped++;
            {
                char m[96]; int k = snprintf(m, sizeof(m),
                    "[CLAMP] cmd=0x80000006 len 0x%x -> 16 (n=%lu)\n",
                    old, n_clamped);
                if (k > 0) write(2, m, k);
            }
            /* 用改过的副本调用（注意：副本在栈上，地址会被内核换成新指针） */
            return (int)syscall(SYS_ioctl, fd, req, buf);
        }
    }
    return (int)syscall(SYS_ioctl, fd, req, arg);
}

__attribute__((destructor))
static void c_report(void)
{
    char m[96]; int k = snprintf(m, sizeof(m), "[CLAMP] clamped=%lu\n", n_clamped);
    if (k > 0) write(2, m, k);
}
