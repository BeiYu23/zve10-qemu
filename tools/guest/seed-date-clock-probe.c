/* Scan 0x900100..0x9001ff for services that answer op=2 (read), then try write. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int osal_snd_sync_direct(unsigned int, void *);
struct msg { unsigned char b[48]; };
static int call(unsigned qid, struct msg *m, unsigned op)
{
    memset(m, 0, sizeof *m);
    m->b[0] = 48;
    m->b[16] = op; m->b[17] = 0;
    m->b[18] = 0xff; m->b[19] = 0x7f;
    return osal_snd_sync_direct(qid, m);
}
int main(void)
{
    struct msg m;
    for (unsigned q = 0x900100; q <= 0x9001ff; q++) {
        int r = call(q, &m, 2);
        int st = (short)(m.b[18] | (m.b[19] << 8));
        if (r == 0 && st == 0)
            printf("DCQ qid=%06x read_ok b20=%02x b32=%02x b46=%02x\n", q, m.b[20], m.b[32], m.b[46]);
    }
    return 0;
}
