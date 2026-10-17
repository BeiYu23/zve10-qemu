/* 最小修复 payload：读 FAT 根目录扇区 → 找 ld.so.preload 项 → 标 0xE5 → 写回
 * 只读写 1 个 512B 扇区。零依赖、可复现。
 */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define SDHCI       0xF0304000u
#define R_BLKSIZE   0x04
#define R_BLKCNT    0x06
#define R_ARG       0x08
#define R_TRNMOD    0x0C
#define R_CMD       0x0E
#define R_RESP      0x10
#define R_BUF       0x20
#define R_PSTATE    0x24
#define R_INTSTAT   0x30
#define R_INTEN     0x34
#define R_ERREN     0x36
#define R_CAP       0x40
#define R_CLKCTL    0x2C
#define R_HOSTCTL   0x28
#define R_PWRCTL    0x29
#define R_SWRST     0x2F
#define R_INTSTEN   0x34

#define REG32(a)  (*(volatile u32 *)(a))
#define REG16(a)  (*(volatile u16 *)(a))
#define REG8(a)   (*(volatile u8  *)(a))

#define IS_CMDCPL  0x0001
#define IS_XFERCPL 0x0002
#define IS_BUFWR   0x0010
#define IS_BUFRD   0x0020
#define IS_ERR     0x8000

#define F_SHORT 0x02
#define F_CRC   0x08
#define F_IDX   0x10
#define F_DATA  0x20
#define F_BUSY  0x03
#define T_MULTI 0x20
#define T_ACMD12 0x04
#define T_BLKCNTEN 0x02
#define T_READ  0x10

#define UART 0xF2000000u
#define UART_FR (UART + 0x18)
#define UART_DR (UART + 0x00)

static void uputc(char c){ int g=2000000; while((REG32(UART_FR)&(1u<<5))&&--g){} REG32(UART_DR)=(u32)(u8)c; }
static void uputs(const char*s){ while(*s) uputc(*s++); }
static void unl(void){ uputc('\r'); uputc('\n'); }
static void uhex8(u32 v){ static const char h[]="0123456789abcdef"; uputc(h[(v>>4)&0xF]); uputc(h[v&0xF]); }
static void uhex32(u32 v){ for(int i=28;i>=0;i-=4) uhex8((v>>i)&0xF); }
/* 不用除法/取模，避免 __aeabi_uidivmod（用重复减法） */
static void udec(u32 v){
    char b[12]; int i=0;
    if(!v){ uputc('0'); return; }
    while(v && i<11){
        u32 q=0, r=v;
        while(r>=10){ r-=10; q++; }
        b[i++]='0'+(char)r; v=q;
    }
    while(i--) uputc(b[i]);
}
static void mdelay(u32 ms){ for(volatile u32 i=0;i<ms*2000u;i++){} }

static int wait_inhibit(u32 mask,u32 to){ for(u32 t=0;t<to;t++){ if((REG32(SDHCI+R_PSTATE)&mask)==0) return 0; mdelay(1);} return -1; }
static int wait_int(u32 mask,u32 to){ for(u32 t=0;t<to;t++){ u32 s=REG32(SDHCI+R_INTSTAT); if(s&IS_ERR){ REG32(SDHCI+R_INTSTAT)=IS_ERR; return -2;} if(s&mask){ REG32(SDHCI+R_INTSTAT)=mask; return 0;} mdelay(1);} return -1; }

static int sdhci_cmd(u32 idx,u32 arg,u32 flags,u32 *resp){
    if(wait_inhibit(0x3,1000)) return -1;
    REG32(SDHCI+R_ARG)=arg; __asm__ volatile("dsb");
    REG16(SDHCI+R_CMD)=(u16)((idx<<8)|(flags&0xFF)); __asm__ volatile("dsb");
    if(wait_int(IS_CMDCPL,1000)) return -1;
    if(resp) *resp=REG32(SDHCI+R_RESP);
    return 0;
}
static void sdhci_reset_cd(void){ REG8(SDHCI+R_SWRST)=0x06; for(int i=0;i<500;i++){ if(!(REG8(SDHCI+R_SWRST)&0x06)) break; mdelay(1);} }
static int sdhci_set_clock(u32 div){
    u16 clk=REG16(SDHCI+R_CLKCTL);
    REG16(SDHCI+R_CLKCTL)= clk & ~0x0004;
    clk=(u16)(((div&0xFF)<<8)|(((div>>8)&3)<<6)|0x0001);
    REG16(SDHCI+R_CLKCTL)=clk;
    for(int i=0;i<500;i++){ if(REG16(SDHCI+R_CLKCTL)&0x0002) break; mdelay(1);}
    REG16(SDHCI+R_CLKCTL)=clk|0x0004;
    mdelay(2);
    return 0;
}

static int rd_block(u32 blk, u8 *buf){
    u32 *o=(u32*)buf;
    if(wait_inhibit(0x3,1000)) return -1;
    REG16(SDHCI+R_BLKSIZE)=512;
    REG16(SDHCI+R_BLKCNT)=1;
    REG16(SDHCI+R_TRNMOD)=T_READ;
    __asm__ volatile("dsb");
    REG32(SDHCI+R_INTSTAT)=REG32(SDHCI+R_INTSTAT);
    if(sdhci_cmd(17,blk,F_SHORT|F_CRC|F_IDX|F_DATA,0)) return -2;
    if(wait_int(IS_BUFRD,1000)) return -3;
    for(u32 w=0;w<128;w++) *o++=REG32(SDHCI+R_BUF);
    if(wait_int(IS_XFERCPL,2000)) return -4;
    return 0;
}
static int wr_block(u32 blk, const u8 *buf){
    const u32 *in=(const u32*)buf;
    if(wait_inhibit(0x3,1000)) return -1;
    REG16(SDHCI+R_BLKSIZE)=512;
    REG16(SDHCI+R_BLKCNT)=1;
    REG16(SDHCI+R_TRNMOD)=T_BLKCNTEN;      /* 写：不设 TRNS_READ/MULTI/DMA */
    __asm__ volatile("dsb");
    REG32(SDHCI+R_INTSTAT)=REG32(SDHCI+R_INTSTAT);
    if(sdhci_cmd(24,blk,F_SHORT|F_CRC|F_IDX|F_DATA,0)) return -2;   /* CMD24 单块写 */
    if(wait_int(IS_BUFWR,1000)) return -3;
    for(u32 w=0;w<128;w++) REG32(SDHCI+R_BUF)=*in++;
    if(wait_int(IS_XFERCPL,2000)) return -4;
    return 0;
}

/* 目标：根目录第 4 个扇区 = 分区扇区 8 ⇒ eMMC 块 0x56008 */
#define ROOT_SCAN_BLK   0x56008u

int main(void){
    u8 buf[512];
    int r, found = -1;
    uputs("==== ZV10 MINIMAL FIX: delete /etc/ld.so.preload ===="); unl();

    /* 1) SDHCI 基础初始化 */
    sdhci_reset_cd();
    REG8(SDHCI+R_HOSTCTL)=0x00;
    { u32 cap=REG32(SDHCI+R_CAP); u32 bk=((cap>>8)&0xFF)*1000; if(!bk) bk=48000;
      u32 div=bk/(2*400); if(!div) div=1; sdhci_set_clock(div); }
    REG8(SDHCI+R_PWRCTL)=0x0F;
    REG16(SDHCI+R_INTEN)=0x0033;
    REG16(SDHCI+R_ERREN)=0x03FF;

    /* 2) CMD0/1/2/3/7 */
    sdhci_cmd(0,0,0,0); mdelay(2);
    for(int t=0;t<1000;t++){ u32 r1; if(sdhci_cmd(1,0x40FF8080,F_SHORT,&r1)==0 && (r1&0x80000000u)) break; mdelay(1); }
    if(sdhci_cmd(2,0,F_SHORT|0x01|F_CRC,0)) { uputs("!! CMD2 fail"); unl(); for(;;); }
    if(sdhci_cmd(3,0x10000,F_SHORT|F_CRC|F_IDX,0)) { uputs("!! CMD3 fail"); unl(); for(;;); }
    if(sdhci_cmd(7,0x10000,F_BUSY|F_CRC|F_IDX,0)) { uputs("!! CMD7 fail"); unl(); for(;;); }
    uputs(" [1] mmc init OK"); unl();

    /* 3) 读根目录扇区 */
    r = rd_block(ROOT_SCAN_BLK, buf);
    uputs(" [2] read blk 0x56008 ret="); udec((u32)r); unl();
    if(r) { for(;;); }

    /* 4) 打印前几个目录项，便于人工确认 */
    for(int i=0;i<16;i++){
        u8 *e=buf+i*32;
        if(e[0]==0x00) { uputs("   [end]"); unl(); break; }
        if(e[0]==0xE5) continue;
        uputs("   ["); udec(i); uputs("] first=0x"); uhex8(e[0]);
        uputs(" attr=0x"); uhex8(e[11]);
        uputs(" name=");
        for(int k=0;k<11;k++){ u8 c=e[k]; uputc((c>=32&&c<127)?(char)c:'.'); }
        unl();
    }

    /* 5) ★ LFN-aware 扫描：解析 LFN 槽位拼出全名，或退化为短名首2字符匹配 */
    {
        int lfn_seq[20], lfn_n = 0;
        char lfn_name[64]; int ln = 0;
        int hit = -1;
        for (int i = 0; i < 16; i++) {
            u8 *e = buf + i*32;
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5) { lfn_n = 0; ln = 0; continue; }

            if ((e[11] & 0x0F) == 0x0F) {
                /* LFN 槽位：名字是 UTF-16LE，位于 1..10 / 14..25 / 28..31 */
                lfn_seq[lfn_n++] = e[0] & 0x1F;
                int offs[3] = {1, 14, 28}, cnts[3] = {5, 6, 2};
                for (int k = 0; k < 3; k++) {
                    for (int z = 0; z < cnts[k]; z++) {
                        u16 ch = (u16)e[offs[k]+z*2] | ((u16)e[offs[k]+z*2+1] << 8);
                        if (ch == 0x0000 || ch == 0xFFFF) continue;
                        if (ch < 128 && ln < 62) lfn_name[ln++] = (char)ch;
                    }
                }
                continue;
            }

            /* 短名槽位：先看 LFN 拼出的名字里是否含 "preload"（大小写不敏感） */
            lfn_name[ln] = 0;
            int is_target = 0;
            if (ln >= 7) {
                for (int p = 0; p + 7 <= ln; p++) {
                    char c0=lfn_name[p],c1=lfn_name[p+1],c2=lfn_name[p+2],c3=lfn_name[p+3];
                    char c4=lfn_name[p+4],c5=lfn_name[p+5],c6=lfn_name[p+6];
                    /* "preload" */
                    if ((c0=='p'||c0=='P')&&(c1=='r'||c1=='R')&&(c2=='e'||c2=='E')&&
                        (c3=='l'||c3=='L')&&(c4=='o'||c4=='O')&&(c5=='a'||c5=='A')&&(c6=='d'||c6=='D'))
                        { is_target = 1; break; }
                }
            }
            /* 退化判定：短名前 2 字符 == "LD"，扩展名前 3 字符 == "PRE" */
            if (!is_target && (e[0]=='L'||e[0]=='l') && (e[1]=='D'||e[1]=='d')) {
                if ((e[8]=='P'||e[8]=='p')&&(e[9]=='R'||e[9]=='r')&&(e[10]=='E'||e[10]=='e'))
                    is_target = 1;
            }

            if (is_target) {
                hit = i;
                uputs(" [3] HIT short entry "); udec((u32)i);
                uputs("  LFN=[");
                for (int q = 0; q < ln; q++) uputc(lfn_name[q] >= 32 ? lfn_name[q] : '.');
                uputs("]"); unl();
                break;
            }
            lfn_n = 0; ln = 0;
        }

        if (hit < 0) {
            uputs(" [3] NOT FOUND in this sector"); unl();
            for (;;);
        }

        /* 6) ★ 档2：清 size 字段（+28..+31）= 0 ⇒ 文件存在但 0 字节 ⇒ preload 列表为空 */
        u8 *se = buf + hit*32;
        u32 oldsize = (u32)se[28] | ((u32)se[29]<<8) | ((u32)se[30]<<16) | ((u32)se[31]<<24);
        uputs(" [4] 原 size=0x"); uhex32(oldsize); unl();
        se[28]=0; se[29]=0; se[30]=0; se[31]=0;
        uputs(" [4] size 已清 0（档2：文件变 0 字节，preload 失效）"); unl();

        /* 7) 写回 */
        r = wr_block(ROOT_SCAN_BLK, buf);
        uputs(" [5] write back ret="); udec((u32)r); unl();
        if (r) { for(;;); }

        /* 8) 读回验证 */
        { u8 v[512]; int rr = rd_block(ROOT_SCAN_BLK, v);
          uputs(" [6] readback ret="); udec((u32)rr); unl();
          if (!rr) {
            u8 *rs = v + hit*32;
            u32 ns = (u32)rs[28] | ((u32)rs[29]<<8) | ((u32)rs[30]<<16) | ((u32)rs[31]<<24);
            uputs("     读回 size=0x"); uhex32(ns);
            if (ns == 0) uputs("  ✓✓ size 已为 0，preload 失效！");
            else uputs("  ✗ 未生效");
            unl();
          }
        }
    }

    uputs("==== DONE ===="); unl();
    for(;;){}
}
