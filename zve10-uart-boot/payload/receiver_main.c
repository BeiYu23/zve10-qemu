typedef unsigned int u32; typedef unsigned char u8;
#define R32(a) (*(volatile u32*)(a))
#define MISC 0xF2915010u
#define LED  0xF2910048u
#define SET  0xF2910044u
#define DIR  0xF2910004u

static void L(u32 on){ R32(on?LED:SET)=1u; }
/* 与测量循环同代价的延时：都读 MISC */
static void dl(u32 n){ for(volatile u32 i=0;i<n;i++){ (void)R32(MISC); } }
static int line(void){ return (int)((R32(MISC)>>1)&1u); }

static u8 getbyte(void){
  u32 n=0, i;
  while(line()) { }                       /* 等起始位下降沿 */
  while(!line()) { n++; if(n>2000000u) break; }  /* 以起始位宽度为基准 */
  { u32 h=n>>1; for(i=0;i<h;i++){} }      /* 到 bit0 中点 */
  u8 v=0;
  for(int b=0;b<8;b++){
    if(line()) v |= (u8)(1u<<b);
    dl(n);
  }
  dl(2*n);                                 /* 跳过校验位+停止位 */
  return v;
}
static u32 crc32(u32 crc,u8 b){
  crc ^= b;
  for(int k=0;k<8;k++) crc = (crc&1)? (0xEDB88320u^(crc>>1)) : (crc>>1);
  return crc;
}
static void magic_sync(void){
  u32 m=0;
  while(m<4){
    u8 b=getbyte();
    if((m==0||m==2) && b==0xAAu) m++;
    else if((m==1||m==3) && b==0x55u) m++;
    else m = (b==0xAAu)?1:0;
  }
}
int main(void){
  u32 addr,len,want,entry,crc,i;
  R32(0xF2402000u)=0; R32(0xF2402004u)=0x11u; R32(DIR)=1u;
  L(1); dl(1500000); L(0); dl(600000);     /* 1.5 秒长闪自检 */

  for(;;){
    magic_sync();
    addr=0; len=0; want=0; entry=0; crc=0xFFFFFFFFu;
    for(i=0;i<4;i++) addr  |= ((u32)getbyte())<<(8*i);
    for(i=0;i<4;i++) len   |= ((u32)getbyte())<<(8*i);
    for(i=0;i<4;i++) want  |= ((u32)getbyte())<<(8*i);
    /* 合法性检查 */
    if(addr < 0xFE000000u || addr >= 0xFE100000u || len==0 || len > 0x40000u){
      L(1); dl(100000); L(0); dl(100000);   /* 短促双闪=丢弃，重新同步 */
      continue;
    }
    for(i=0;i<4;i++) entry |= ((u32)getbyte())<<(8*i);
    { volatile u8 *p=(volatile u8*)addr;
      u32 idle;
      for(i=0;i<len;i++){
        u8 b=getbyte(); p[i]=b; crc=crc32(crc,b);
        /* 检测线路长时间空闲 = 主机分段空档 → 进度闪一下 */
        idle=0;
        while(line() && idle<200000u){ idle++; }
        if(idle>=60000u){                    /* 明显空档 */
          L(1); { volatile u32 k; for(k=0;k<200000;k++){} } L(0);
          { volatile u32 k; for(k=0;k<200000;k++){} }
        }
      } }
    crc = ~crc;
    if(crc==want){                          /* 通过：循环播报 1 次长闪 */
      if(entry!=0xFFFFFFFFu){               /* 要跳转：先闪一次再跳 */
        L(1); dl(1000000); L(0); dl(400000);
        ((void(*)(void))entry)();
      }
      for(;;){ L(1); dl(1000000); L(0); dl(2000000); }   /* 只校验：无限播报 */
    } else {                                /* 失败：3 次长闪 × 3 遍 */
      for(int r=0;r<3;r++){
        for(i=0;i<3;i++){ L(1); dl(350000); L(0); dl(250000); }
        dl(1500000);
      }
    }
  }
  for(;;){ L(1); dl(200000); L(0); dl(200000); }
}
