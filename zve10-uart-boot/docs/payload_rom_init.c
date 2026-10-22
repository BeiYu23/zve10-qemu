typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define R32(a) (*(volatile u32*)(a))
#define R16(a) (*(volatile u16*)(a))
#define R8(a)  (*(volatile u8*)(a))
#define S 0xF0304000u
#define LED 0xF2910048u
#define SET 0xF2910044u
#define DIR 0xF2910004u
#define BUF 0xFE02E000u
static void L(u32 on){ R32(on?LED:SET)=1u; }
static void dl(u32 n){ for(volatile u32 i=0;i<n;i++){} }
static void bl(u32 n){ for(u32 i=0;i<n;i++){ L(1); dl(1200000); L(0); dl(600000);} }

/* 发一条命令；返回 0=完成 非0=超时 */
static int cmd(u32 idx, u32 arg, u32 flags, u32 ms){
  u32 i;
  R32(S+0x124) |= 0x20000u;
  R32(S+0x30) = 0xFFFFFFFFu;
  R32(S+0x08) = arg;
  R16(S+0x0E) = (u16)((idx<<8)|(flags&0x3F));
  for(i=0;i<ms*2000u;i++){ if(R32(S+0x30)&1u){ R32(S+0x124)&=~0x20000u; return 0; } }
  R32(S+0x124) &= ~0x20000u;
  return -1;
}

int main(void){
  u32 i, res=0, ok;
  R32(0xF2402000u)=0; R32(0xF2402004u)=0x11u; R32(DIR)=1u;
  L(1); dl(1500000); L(0); dl(600000);          /* 自检 1.5 秒 */

  /* ===== ① ROM 的 PHY 块初始化（之前完全缺失）===== */
  R32(0xF0304200u)=0;
  R32(0xF0304208u)=7;
  R32(0xF0304200u)=1;
  R32(0xF0304100u)=7;
  { u32 t=R32(0xF0304204u);
    t&=~2u;                 R32(0xF0304204u)=t;
    t&=~(0x3FFu<<16); t|=0x01E00000u; R32(0xF0304204u)=t;
    t|=2u;                  R32(0xF0304204u)=t; }

  /* ===== ② 标准 SDHCI 初始化（照 ROM）===== */
  R16(S+0x2C)=0x0001u;
  for(i=0;i<10000u;i++){ if(R16(S+0x2C)&2u) break; }
  R8(S+0x28)=(u8)(R8(S+0x28)|0x34u);
  R16(S+0x38)=0; R16(S+0x3A)=0;
  R16(S+0x34)=0x000Bu; R16(S+0x36)=0x77FFu;
  R8(S+0x29)=0x0Au;                     /* POWER = 0x0A（不是 0x0F） */
  R32(S+0x158)=0x40u;
  R32(S+0x124)=0x200u;
  R8(S+0x2E)=0x0Eu;
  R16(S+0x2C)=(u16)(R16(S+0x2C)|0x0004u);

  /* ===== ③ 命令序列 ===== */
  if(cmd(0,0,0x00,100)){ res=5; goto done; }          /* CMD0 */
  for(i=0;i<200u;i++){                                 /* CMD1 */
    if(cmd(1,0x40FF8080u,0x02,50)==0 && (R32(S+0x10)&0x80000000u)) break;
  }
  if(i>=200u){ res=4; goto done; }
  if(cmd(2,0,0x1A,200)){ res=6; goto done; }          /* CMD2 */
  if(cmd(3,0x00010000u,0x1A,200)){ res=6; goto done; }/* CMD3 */
  if(cmd(7,0x00010000u,0x1A,200)){ res=7; goto done; }/* CMD7 */
  cmd(16,512u,0x1A,200);                               /* CMD16 */

  /* ===== ④ 单块读（ROM 方式：Sony 描述符 + TM=0x31）===== */
  R16(S+0x04)=512;
  R16(S+0x06)=0;
  R32(S+0x58)=BUF;
  R32(S+0x5C)=0;
  R16(S+0x0C)=0x31;
  ok = (cmd(17,0x00056000u,0x1A,500)==0);
  if(!ok){ res=3; goto done; }
  for(i=0;i<200000u;i++){ if(R32(S+0x30)&0x0020u) break; }   /* 数据就绪 */
  { volatile u32 *b=(volatile u32*)BUF;
    if((u8)(b[0]&0xFF)==0xEB && (u8)(b[127]>>16)==0x55 && (u8)(b[127]>>24)==0xAA) res=1;
    else res=2; }
done:
  for(;;){ bl(res); dl(2000000); }
}
