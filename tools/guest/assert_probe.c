/* Standalone __assert_fail hook: print assertion + lr + stack words filtered by
 * appFw.so code range. Intended to be MERGED into an existing preload library
 * (adding a second LD_PRELOAD lib has been observed to stall boot).
 *
 * Build (if standalone): arm-linux-gcc -shared -fPIC -O2 -o libassertprobe.so \
 *   assert_probe.c -ldl -Wl,--dynamic-linker=/lib/ld-linux.so.3
 */
#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int lg(const char *fmt, ...)
{
    char b[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n <= 0) return -1;
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return -1;
    int r = (int)write(fd, b, (size_t)n);
    close(fd);
    return r;
}

static uintptr_t g_base;

static int find_base(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz; (void)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "appFw.so")) {
        g_base = (uintptr_t)info->dlpi_addr;
        lg("AP base=0x%08x name=%s\n", (unsigned)g_base, info->dlpi_name);
    }
    return 0;
}

__asm__(".symver __assert_fail,__assert_fail@GLIBC_2.4");

void __assert_fail(const char *assertion, const char *file,
                   unsigned int line, const char *function)
{
    lg("AS a=%s f=%s l=%u\n", assertion ? assertion : "?", file ? file : "?", line);
    lg("AS fn=%s\n", function ? function : "?");
    lg("AS lr=0x%08x\n", (unsigned)(uintptr_t)__builtin_return_address(0));

    if (!g_base) dl_iterate_phdr(find_base, 0);

    uintptr_t spv;
    __asm__ volatile("mov %0, sp" : "=r"(spv));
    uintptr_t *sp = (uintptr_t *)spv;

    for (int i = 0; i < 64; i += 4)
        lg("STK %02x: %08x %08x %08x %08x\n", i * 4,
           (unsigned)sp[i], (unsigned)sp[i + 1],
           (unsigned)sp[i + 2], (unsigned)sp[i + 3]);

    if (g_base) {
        uintptr_t lo = g_base + 0x52ab00;      /* appFw .text VA range (from ELF) */
        uintptr_t hi = g_base + 0x2ce8ac8;
        for (int i = 0; i < 64; i++) {
            uintptr_t v = sp[i] & ~(uintptr_t)1;
            if (v >= lo && v < hi)
                lg("CAND off=0x%08x raw=0x%08x\n",
                   (unsigned)(v - g_base), (unsigned)sp[i]);
        }
    }
    _exit(1);
}
