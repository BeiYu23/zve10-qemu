/* BK4 (Backup.bin) key resolver — standalone, no kernel dependencies.
 *
 * Layout (verified against the file's own header arithmetic):
 *   0x000000  header
 *   0x00010C  page table   819 entries x 6 bytes
 *   0x00143E  slot table 33195 entries x 6 bytes
 *   0x031E40  data area
 *
 * Page entry: { u16 count, u32 cum_base }   cum_base[i+1] = cum_base[i] + count[i]
 * Slot entry: { u16 ?,     u32 v }          v == -1 means empty
 * v:          type = v >> 24, off = v & 0xFFFFFF
 *             type == 0 or 0xFF -> size = u16@off, data at off+2
 *             otherwise         -> size = type,  data at off
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_TABLE_OFF   0x00010Cu
#define SLOT_TABLE_OFF   0x00143Eu
#define PAGE_COUNT       819u

static uint8_t *g_img;
static size_t   g_len;

static uint16_t rd16(size_t o) { return o + 2 <= g_len ? (uint16_t)(g_img[o] | (g_img[o+1] << 8)) : 0; }
static uint32_t rd32(size_t o) { return o + 4 <= g_len ? (uint32_t)(g_img[o] | (g_img[o+1]<<8) | (g_img[o+2]<<16) | ((uint32_t)g_img[o+3]<<24)) : 0; }

int bk4_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= (long)SLOT_TABLE_OFF) { fclose(f); return -2; }
    g_len = (size_t)n;
    g_img = malloc(g_len);
    if (!g_img || fread(g_img, 1, g_len, f) != g_len) { fclose(f); return -3; }
    fclose(f);
    if (memcmp(g_img + 0x0C, "BK4", 3) != 0) return -4;   /* magic */
    return 0;
}

/* Returns 1 on success and fills size/off; 0 if the key is absent. */
int bk4_resolve(uint32_t key, uint32_t *size, uint32_t *off)
{
    uint32_t page = key >> 16, sub = key & 0xFFFFu;
    if (page >= PAGE_COUNT) return 0;

    size_t pe = PAGE_TABLE_OFF + page * 6;
    uint32_t count    = rd16(pe);
    uint32_t cum_base = rd32(pe + 2);
    if (sub >= count) return 0;

    uint32_t slot = cum_base + sub;
    uint32_t v    = rd32(SLOT_TABLE_OFF + slot * 6 + 2);
    if (v == 0xFFFFFFFFu) return 0;

    uint32_t type = v >> 24, o = v & 0xFFFFFFu;
    if (type == 0 || type == 0xFF) {
        *size = rd16(o);
        *off  = o + 2;
    } else {
        *size = type;
        *off  = o;
    }
    return (*off + *size) <= g_len;
}

const uint8_t *bk4_data(uint32_t off) { return g_img + off; }

#ifdef BK4_SELFTEST
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <Backup.bin> <key>...\n", argv[0]); return 2; }
    if (bk4_open(argv[1]) != 0) { fprintf(stderr, "open failed\n"); return 1; }
    for (int i = 2; i < argc; i++) {
        uint32_t key = (uint32_t)strtoul(argv[i], NULL, 0), size, off;
        if (!bk4_resolve(key, &size, &off)) { printf("key 0x%08X  absent\n", key); continue; }
        printf("key 0x%08X  size=%-6u off=0x%06X  ", key, size, off);
        for (uint32_t k = 0; k < size && k < 16; k++) printf("%02X", bk4_data(off)[k]);
        if (size > 16) printf("...");
        printf("\n");
    }
    return 0;
}
#endif
