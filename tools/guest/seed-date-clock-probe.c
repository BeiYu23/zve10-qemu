/* Date service probe: 48-byte message, dump full, try write with date fields. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int osal_snd_sync_direct(unsigned int, void *);
struct msg { unsigned char b[48]; };
static unsigned qid = 0x90013d;
static void dump(const char *t, unsigned char *p)
{
    printf("%s", t);
    for (int i = 0; i < 48; i++) printf("%02x", p[i]);
    printf("\n");
}
static int call(struct msg *m, unsigned op)
{
    m->b[0] = 48;
    m->b[16] = op; m->b[17] = 0;
    m->b[18] = 0xff; m->b[19] = 0x7f;
    return osal_snd_sync_direct(qid, m);
}
int main(void)
{
    struct msg m;
    memset(&m, 0, sizeof m);
    int r = call(&m, 2);
    char t[40]; snprintf(t, sizeof t, "DC read2 r=%d:", r); dump(t, m.b);
    /* try write: date 2020-01-01 00:00:00 at +20, state=1 at +32, init=4 at +44 */
    memset(&m, 0, sizeof m);
    unsigned char *p = m.b + 20;
    p[0] = 0xe4; p[1] = 0x07;           /* year 2020 */
    p[2] = 1; p[3] = 1; p[4] = 0; p[5] = 0; p[6] = 0;
    p[12] = 0;
    m.b[32] = 1;
    m.b[44] = 4;
    r = call(&m, 0);
    snprintf(t, sizeof t, "DC write0 r=%d:", r); dump(t, m.b);
    memset(&m, 0, sizeof m);
    r = call(&m, 2);
    snprintf(t, sizeof t, "DC reread r=%d:", r); dump(t, m.b);
    return 0;
}
