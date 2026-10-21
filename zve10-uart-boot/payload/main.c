typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define R32(a) (*(volatile u32*)(a))
#define R16(a) (*(volatile u16*)(a))
#define R8(a)  (*(volatile u8*)(a))
#define S 0xF0304000u
#define LED 0xF2910048u
#define SET 0xF2910044u
#define DIR 0xF2910004u
#define TMR 0xF240310Cu
static void d(u32 ms){ volatile u32*t=(volatile u32*)TMR; u32 s=*t,g=0,tg=ms*4032u;
  while((u32)(*t-s)<tg){ if(++g>20000000u) break; } }
static void L(u32 on){ R32(on?LED:SET)=1u; }
static int wi(u32 m,u32 ms){ for(u32 i=0;i<ms*4u;i++){ if(R32(S+0x30)&m) return 0; d(1);} return -1; }
static int C(u32 i,u32 a,u32 f,u32 ms){
  for(int w=0;w<100;w++){ if((R32(S+0x24)&1u)==0) break; d(1); }
  R32(S+0x30)=0xFFFFFFFFu; R32(S+0x08)=a;
  R16(S+0x0E)=(u16)((i<<8)|(f&0x3F));
  return wi(1,ms);
}
static int rdblk(u32 lba,u32 *b){
  R16(S+0x04)=512; R16(S+0x06)=1; R16(S+0x0C)=0x0012;
  C(17,lba,0x1E,100);
  if(wi(0x0021u,150)) return -1;
  for(int i=0;i<128;i++) b[i]=R32(S+0x20);
  return 0;
}
int main(void){
  R32(0xF2402000u)=0; R32(0xF2402004u)=0x11u; R32(DIR)=1u;
  { u16 ck=R16(S+0x2C); R16(S+0x2C)=(u16)(ck&~4u); d(3);
    R16(S+0x2C)=(u16)((5<<8)|1u);
    for(int w=0;w<200;w++){ if(R16(S+0x2C)&2u) break; d(1);}
    R16(S+0x2C)=(u16)(((5<<8)|1u)|4u); }
  d(20); R8(S+0x29)=0x0F; d(3); R8(S+0x2E)=0x0E;
  R16(S+0x34)=0x0033; R16(S+0x36)=0x03FF;
  C(0,0,0x00,100);
  for(int t=0;t<60;t++){ if(C(1,0x40FF8080u,0x02,50)==0 && (R32(S+0x10)&0x80000000u)) break; d(2); }
  C(2,0,0x05,200); C(3,0x10000u,0x0E,200); C(7,0x10000u,0x0E,200);
  u32 res=4;
  { volatile u32 *b=(volatile u32*)0xFE02E000u;
    if(rdblk(0x56000u,(u32*)b)==0){
      if((u8)(b[0]&0xFF)==0xEB && (u8)(b[127]>>16)==0x55 && (u8)(b[127]>>24)==0xAA) res=1;
      else res=2;
    } else res=3; }
  for(u32 i=0;i<res;i++){ L(1); d(600); L(0); d(300); }
  d(1200);
  for(;;){ L(1); d(2000); L(0); d(500); }
}
