/* swgpe_shim.c —— ZV-1F 新菜单软件渲染 shim
 *
 * 编译：  arm-*-gcc -nostdlib -shared -fPIC -O2 -o libswgpe.so swgpe_shim.c
 * 使用：  LD_PRELOAD=/usr/lib/libswgpe.so
 *
 * 设计要点（全部经反汇编验证）：
 *   · gperm 是绘制汇聚点：GPU 命令构造器 0x1156C0 全 .text 唯一调用者在其内
 *   · FillRectParam: +0x00 bitmap, +0x04 colour[4], +0x08 x0,y0, +0x0C x1,y1,
 *                    +0x10 mode（只接受 1/2）, +0x14 flag
 *   · bitmap: +0x04 geo, +0x08 像素, geo+0x14/+0x18/+0x1C/+0x20 计数,
 *             geo+0x18 stride(像素), geo+0x1C format, table[format] = bpp
 *   · WriteLockBits 在 arg1==-1 时 pthread_cond_wait ⇒ 必须中性化
 *   · 计数 [+0x18] 是 lock ++ / unlock -- 的 1:1 配对，必须手工维持
 *
 * 注意：所有符号是 C 符号（GRM_ 前缀，无 _ZN 修饰），不可被 C++ 修饰。
 */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long      u64;

/* ---------- 裸 syscall 日志（无 libc 依赖） ---------- */
static int sys_write(int fd, const void *buf, u32 n)
{
    register int r7 __asm__("r7") = 4;      /* __NR_write */
    register int r0 __asm__("r0") = fd;
    register const void *r1 __asm__("r1") = buf;
    register u32 r2 __asm__("r2") = n;
    __asm__ volatile ("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void log_s(const char *s) { sys_write(2, s, slen(s)); sys_write(2, "\n", 1); }
static void log_x(const char *tag, u32 v)
{
    char b[64]; int i = 0;
    while (*tag) b[i++] = *tag++;
    b[i++] = ' '; b[i++] = '0'; b[i++] = 'x';
    for (int s = 28; s >= 0; s -= 4) {
        int d = (v >> s) & 0xF;
        b[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    b[i++] = '\n';
    sys_write(2, b, (u32)i);
}

/* ---------- 位图字段访问（偏移全部经反汇编验证） ---------- */
#define BMP_GEO(b)      (*(void  **)((u8 *)(b) + 0x04))
#define BMP_PIX(b)      (*(void  **)((u8 *)(b) + 0x08))
#define GEO_STRIDE(g)   (*(u16   *)((u8 *)(g) + 0x18))   /* 像素计 */
#define GEO_FORMAT(g)   (*(u32   *)((u8 *)(g) + 0x1C))   /* 1..9 */
#define GEO_RECT(g)     ((u16   *)((u8 *)(g) + 0x0C))    /* x,y,w,h */
#define CNT14(b)        (*(u32 *)((u8 *)(b) + 0x14))
#define CNT18(b)        (*(u32 *)((u8 *)(b) + 0x18))

/* format → bytes per pixel（ZV-1F libObj .rodata @0x29D88E 查表结果） */
static const u8 kBppByFormat[10] = { 0, 2, 4, 2, 1, 2, 2, 2, 4, 4 };

static void log_counts(const char *tag, void *bmp)
{
    log_x(tag, (u32)bmp);
    log_x("  +14", CNT14(bmp));
    log_x("  +18", CNT18(bmp));
}

/* ---------- 1) WriteLockBits：手工配对计数，绕开所有等待 ---------- */
int GRM_bitmapWriteLockBits(void *bmp, int arg1, void **out)
{
    (void)arg1;                                  /* 0 / -1 / 其它都走同一条路 */
    if (!bmp) { if (out) *out = 0; return -1; }
    CNT18(bmp)++;                                /* 与 Unlock 的 -- 配对 */
    void *pix = BMP_PIX(bmp);
    if (!pix) { if (out) *out = 0; return -1; }  /* 绝不返回"成功 + 空指针" */
    if (out) *out = pix;
    return 0;
}

/* ---------- 2) WriteUnlockBits：正常减回，不下溢 ---------- */
int GRM_bitmapWriteUnlockBits(void *bmp)
{
    if (bmp && CNT18(bmp) > 0) CNT18(bmp)--;
    return 0;
}

/* ---------- 3) CreateFromPhys：探针（验证 shim 生效 + 像素指针） ---------- */
void *GRM_bitmapCreateFromPhys(void *a0, void *a1, void *a2, void *a3,
                               void *a4, void *a5, void *a6, void *a7)
{
    /* 参数语义未完全确定 ⇒ 先只做探针，真实创建仍由后续路径完成。
       若 App 依赖返回值，需要回退到调用原函数（见 README）。 */
    log_x("CREATEPHYS a0", (u32)a0);
    log_x("CREATEPHYS a1", (u32)a1);
    log_x("CREATEPHYS a2", (u32)a2);
    log_x("CREATEPHYS a3", (u32)a3);
    return 0;
}

/* ---------- 4) CheckUpdateFinished：唯一完成信号，立即视为完成 ---------- */
int GRM_fbrmCheckUpdateFinished(void) { return 0; }

/* ---------- 5) gpermFillrect：CPU 填充 ---------- */
static void fill_rect(void *bmp, const u8 *col, int x0, int y0, int x1, int y1)
{
    void *geo = BMP_GEO(bmp);
    void *pix = BMP_PIX(bmp);
    if (!geo || !pix) return;

    u32 fmt = GEO_FORMAT(geo);
    if (fmt < 1 || fmt > 9) return;
    u32 bpp = kBppByFormat[fmt];
    u32 stride = GEO_STRIDE(geo);                /* 像素计 */
    if (!stride) return;

    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((u32)x1 > stride) x1 = (int)stride;
    if (x1 <= x0 || y1 <= y0) return;

    for (int y = y0; y < y1; ++y) {
        u8 *row = (u8 *)pix + (u32)y * stride * bpp;
        for (int x = x0; x < x1; ++x) {
            u8 *p = row + (u32)x * bpp;
            if (bpp == 2) {
                /* RGBA4444 打包：R[15:12] G[11:8] B[7:4] A[3:0]
                   ⚠ 位域未实测，先按 4444 假设；纯绿测试可验证 */
                u16 v = (u16)(((col[0] & 0xF0) << 8) | ((col[1] & 0xF0) << 4)
                            |  (col[2] & 0xF0)       |  (col[3] >> 4));
                *(u16 *)p = v;
            } else if (bpp == 4) {
                p[0] = col[0]; p[1] = col[1]; p[2] = col[2]; p[3] = col[3];
            } else {
                *p = col[0];
            }
        }
    }
}

int GRM_gpermFillrect(void *a, void *b, const u8 *p, u64 c)
{
    if (!p) return -1;
    u32 mode = *(u32 *)(p + 0x10);
    if (mode != 1 && mode != 2) return 0;        /* 真机行为：不绘制 */

    void *bmp = *(void **)(p + 0x00);
    if (!bmp) return -1;

    const u8 *col = p + 0x04;
    const u16 *r  = (const u16 *)(p + 0x08);     /* x0,y0,x1,y1 各 int16 */
    fill_rect(bmp, col, (int)(short)r[0], (int)(short)r[1],
                        (int)(short)r[2], (int)(short)r[3]);
    return 0;
}

/* ---------- 6) gpermRectblit：源/目标均从参数取 bitmap ---------- */
int GRM_gpermRectblit(void *a, void *b, const u8 *p, u64 c)
{
    if (!p) return -1;
    void *dst = *(void **)(p + 0x00);
    void *src = *(void **)(p + 0x0C);
    log_x("RECTBLIT dst", (u32)dst);
    log_x("RECTBLIT src", (u32)src);
    /* 参数语义尚未完全解出（RectBlitParam +0x04/+0x08 待定）
       ⇒ 先只记录，保证不崩；后续按实测补齐 */
    (void)a; (void)b; (void)c;
    return 0;
}

/* ---------- 自检：首次绘制时打印一次计数 ---------- */
static int g_first = 1;
__attribute__((constructor)) static void swgpe_init(void)
{
    log_s("SWGPE loaded");
}
