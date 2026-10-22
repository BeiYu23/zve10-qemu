typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define R32(a) (*(volatile u32*)(a))
#define R16(a) (*(volatile u16*)(a))
#define R8(a)  (*(volatile u8*)(a))
#define S 0xF0304000u
#define LED 0xF2910048u
#define SET 0xF2910044u
#define DIR 0xF2910004u
#define DESC 0xFE02E800u
#define BUF  0xFE02E000u
static void L(u32 on){ R32(on?LED:SET)=1u; }
static void dl(u32 n){ for(volatile u32 i=0;i<n;i++){} }
static void bl(u32 n){ for(u32 i=0;i<n;i++){ L(1); dl(1200000); L(0); dl(600000);} }
static int cmd(u32 idx,u32 arg,u32 flags,u32 ms){
  u32 i;
  R32(S+0x124) |= 0x20000u;
  R32(S+0x30)=0xFFFFFFFFu;
  R32(S+0x08)=arg;
  R16(S+0x0E)=(u16)((idx<<8)|(flags&0x3F));
  for(i=0;i<ms*2000u;i++){ if(R32(S+0x30)&1u){ R32(S+0x124)&=~0x20000u; return 0; } }
  R32(S+0x124)&=~0x20000u;
  return -1;
}
int main(void){
  u32 i,res=0;
  R32(0xF2402000u)=0; R32(0xF2402004u)=0x11u; R32(DIR)=1u;
  L(1); dl(1500000); L(0); dl(600000);

  /* PHY 六步 */
  R32(0xF0304200u)=0; R32(0xF0304208u)=7; R32(0xF0304200u)=1;
  R32(0xF0304100u)=7;
  { u32 t=R32(0xF0304204u);
    t&=~2u; R32(0xF0304204u)=t;
    t&=~(0x3FFu<<16); t|=0x01E00000u; R32(0xF0304204u)=t;
    t|=2u; R32(0xF0304204u)=t; }

  /* SDHCI 初始化（照 ROM）*/
  R16(S+0x2C)=0x0001u;
  for(i=0;i<10000u;i++){ if(R16(S+0x2C)&2u) break; }
  R8(S+0x28)=(u8)(R8(S+0x28)|0x34u);
  R16(S+0x38)=0; R16(S+0x3A)=0;
  R16(S+0x34)=0x000Bu; R16(S+0x36)=0x77FFu;
  R8(S+0x29)=0x0Au;
  R32(S+0x158)=0x40u;
  R32(S+0x124)=0x200u;
  R8(S+0x2E)=0x0Eu;
  R16(S+0x2C)=(u16)(R16(S+0x2C)|0x0004u);

  if(R32(S+0x24)&1u){ res=7; goto done; }   /* 命令抑制位先卡住 */

  /* ① CMD13：ARG=RCA=1<<16，看卡是否已就绪（窗口 2s，照 ROM 上限）*/
  if(cmd(13,0x00010000u,0x1A,2000)){ res=2; goto done; }  /* 无响应 */
  if(R32(S+0x10) & 0xE0F82000u){ res=6; goto done; }      /* RESP0 报错 */
  if(R32(S+0x1C) & 0xE0F82000u){ res=8; goto done; }      /* RESP3 报错 */
  res=1;

  /* ② CMD7 选中 */
  cmd(7,0x00010000u,0x1A,2000);

  /* ③ CMD18 多块读 1 块（ROM 方式）*/
  { volatile u32 *d=(volatile u32*)DESC;
    d[0]=(512u<<16)|0x21u|0x02u;
    d[1]=BUF; }
  R16(S+0x04)=512;
  R16(S+0x06)=1;               /* 块数=1 */
  R32(S+0x58)=DESC;
  R32(S+0x5C)=0;
  R16(S+0x0C)=0x37;            /* DMA|MULTI|READ */
  if(cmd(18,0x00056000u,0x1A,2000)){ res=3; goto done; }  /* 命令超时 */
  for(i=0;i<600000u;i++){ if(R32(S+0x30)&0x0020u) break; }
  { volatile u32 *b=(volatile u32*)BUF;
    if((u8)(b[0]&0xFF)==0xEB && (u8)(b[127]>>16)==0x55 && (u8)(b[127]>>24)==0xAA)
      res=4;                 /* ★ 读通 */
    else res=5; }            /* 收到数据但内容不对 */
done:
  for(;;){ bl(res); dl(2000000); }
}
