/* Boot tracer: records that init reached this exec, then hands over to the
 * real binary. Kept deliberately small and dependency-free so it cannot itself
 * be the thing that fails.
 *
 * Installed as /usr/bin/bootin.elf with the original moved to bootin.real.
 */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static int append(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd < 0)
        return -1;
    size_t n = strlen(text);
    ssize_t w = write(fd, text, n);
    close(fd);
    return (w == (ssize_t)n) ? 0 : -1;
}

int main(int argc, char **argv, char **envp)
{
    char buf[512];
    int n = 0;
    const char *tag = "TRACE bootin-reached argv=";
    while (*tag && n < 400)
        buf[n++] = *tag++;
    for (int i = 0; i < argc && i < 8 && n < 460; i++) {
        const char *a = argv[i];
        while (*a && n < 460)
            buf[n++] = *a++;
        buf[n++] = ' ';
    }
    buf[n++] = '\n';
    buf[n] = 0;

    /* Two locations: /setting survives everything, /tmp is RAM-backed but the
     * partition is real so it also survives a reset. */
    append("/setting/zvegisk/trace", buf);
    append("/tmp/trace_bootin", buf);

    execve("/usr/bin/bootin.real", argv, envp);
    /* If execve failed, say so before dying. */
    append("/setting/zvegisk/trace", "TRACE bootin.execve-FAILED\n");
    return 127;
}
