/* a7c2-style 24-byte message (size field 48) + try several write op encodings. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int osal_snd_sync_direct(unsigned int, void *);
struct m24 { unsigned char b[24]; };
static unsigned qid = 0x90013d;
static int call(struct m24 *m, unsigned op)
{
    m->b[0] = 48;
    m->b[16] = op & 0xff; m->b[17] = (op >> 8) & 0xff;
    m->b[18] = 0xff; m->b[19] = 0x7f;
    return osal_snd_sync_direct(qid, m);
}
static void show(const char *t, struct m24 *m, int r)
{
    printf("%s r=%d:", t, r);
    for (int i = 0; i < 24; i++) printf("%02x", m->b[i]);
    printf("\n");
}
int main(void)
{
    struct m24 m;
    /* baseline read */
    memset(&m, 0, sizeof m);
    int r = call(&m, 2);
    show("S_read2", &m, r);
    /* write attempts, differing op encodings, same payload */
    unsigned ops[] = {0, 0x100, 0x200, 0x300, 0x400, 0x0c};
    for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        memset(&m, 0, sizeof m);
        unsigned char *p = m.b + 20;   /* date at +20 (a7c2 layout) */
        p[0] = 0xe4; p[1] = 0x07; p[2] = 1; p[3] = 1;   /* 2020-01-01 */
        m.b[32 - 8] = 1;               /* won't fit 24B; keep for reference */
        r = call(&m, ops[i]);
        char t[24]; snprintf(t, sizeof t, "S_ww%03x", ops[i]);
        show(t, &m, r);
    }
    memset(&m, 0, sizeof m);
    r = call(&m, 2);
    show("S_read2b", &m, r);
    return 0;
}
