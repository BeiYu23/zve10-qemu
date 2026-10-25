/* zvegisk_dumpface.c — 把某个 /dev/stream 面 dump 成文件（供 PULL 回来查看）
 * 参数: dev off len outfile
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: %s <dev> <off> <len> <out> <logfile>\n", argv[0]); return 1; }
    const char *dev = argv[1];
    unsigned long off = strtoul(argv[2], 0, 0);
    unsigned long len = strtoul(argv[3], 0, 0);
    const char *outp = argv[4];
    const char *logp = argv[5];

    int lf = open(logp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    #define LOG(...) do { char b[256]; int n=snprintf(b,sizeof b,__VA_ARGS__); if(lf>=0)write(lf,b,n); } while(0)

    LOG("dump dev=%s off=0x%lx len=0x%lx\n", dev, off, len);
    int fd = open(dev, O_RDONLY);
    if (fd < 0) { LOG("open FAIL %s\n", strerror(errno)); if(lf>=0)close(lf); return 1; }
    void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, (off_t)off);
    if (p == MAP_FAILED) { LOG("mmap FAIL %s\n", strerror(errno)); close(fd); if(lf>=0)close(lf); return 1; }
    LOG("mapped at %p\n", p);

    int of = open(outp, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (of < 0) { LOG("create FAIL %s\n", strerror(errno)); munmap(p,len); close(fd); if(lf>=0)close(lf); return 1; }
    ssize_t w = write(of, p, len);
    LOG("wrote %zd bytes to %s\n", w, outp);
    close(of);
    munmap(p, len); close(fd);
    if (lf>=0) close(lf);
    return 0;
}
