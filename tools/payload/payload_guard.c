/* payload_guard.c — 武装守卫：预检通过才允许应用 UI 改动
 *
 * 背景（来自 agent_v14 的设计）：
 *   agent_v14 的看门狗保证"UI 90 秒不起来就 umount + 重启回原厂"。
 *   但它只判断"UI 有没有起来"，不判断"我们给的替换文件本身对不对"。
 *   当初变砖正是因为一个坏 .so 被直接武装上去。
 *
 * 本载荷补上【武装前的闸门】：
 *   ① 读清单 /log/zv1f_ui/manifest.txt（每行：源 <TAB> 目标）
 *   ② 逐项预检：存在 / 非空 / ELF magic / e_machine==ARM(40) / 非全零
 *   ③ 全过 → 写 /log/zv1f_ui/bind.txt（供 agent_v14 的 apply_binds 使用）
 *   ④ 任一失败 → 删掉 bind.txt（保持不武装）+ 详细日志
 *
 * 安全模式：/log/zv1f_ui/safemode 存在 ⇒ 无条件拒绝武装（并清 bind.txt）
 *
 * 输出：/log/zv1f_ui/guard.log（同时镜像到 /tmp/jiritsu/probe.txt 便于 USB 读回）
 */
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_unlink 10
#define SYS_mkdir 39

static inline int s0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int s1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory","cc");return r0;}
static inline int s2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory","cc");return r0;}
static inline int s3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory","cc");return r0;}
static inline int s4(int n,int a,int b,int c,int d){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3):"memory","cc");return r0;}

#define MANIFEST "/log/zv1f_ui/manifest.txt"
#define BINDF    "/log/zv1f_ui/bind.txt"
#define SAFEMODE "/log/zv1f_ui/safemode"
#define GUARDLOG "/log/zv1f_ui/guard.log"
#define PROBEOUT "/tmp/jiritsu/probe.txt"
#define DIR      "/log/zv1f_ui"

static char LOG[3072]; static int LP;
static void L(const char*s){while(*s)LOG[LP++]=*s++;}
static void LD(long v){char t[16];int n=0;if(v<0){LOG[LP++]='-';v=-v;}if(!v){LOG[LP++]='0';return;}while(v&&n<15){t[n++]='0'+(int)(v%10);v/=10;}while(n)LOG[LP++]=t[--n];}
static void LK(const char*k,long v){L(k);LD(v);L("\r\n");}

static int exists(const char*p){int fd=s3(SYS_open,(int)p,0,0);if(fd<0)return 0;s1(SYS_close,fd);return 1;}
static int slurp(const char*p,char*b,int cap){int fd=s3(SYS_open,(int)p,0,0);if(fd<0)return -1;int n=s3(SYS_read,fd,(int)b,cap-1);s1(SYS_close,fd);if(n<0)return -1;b[n]=0;return n;}

/* 预检单个源文件：返回 0 通过，负数失败码 */
static long precheck(const char *src, long *size_out)
{
    int fd=s3(SYS_open,(int)src,0,0);
    if(fd<0) return -1;                       /* 打不开 */
    unsigned char hdr[64];
    int n=s3(SYS_read,fd,(int)hdr,64);
    if(n<4){s1(SYS_close,fd);return -2;}      /* 太小 */
    /* ELF magic */
    if(!(hdr[0]==0x7f&&hdr[1]=='E'&&hdr[2]=='L'&&hdr[3]=='F')){s1(SYS_close,fd);return -3;}
    /* 32 位? (EI_CLASS==1) */
    if(hdr[4]!=1){s1(SYS_close,fd);return -4;}
    /* e_machine == 40 (ARM) */
    if(n>=20){ unsigned m=(unsigned)hdr[18]|((unsigned)hdr[19]<<8);
               if(m!=40){s1(SYS_close,fd);return -5;} }
    /* 统计大小 + 是否全零（读到 EOF 累计） */
    long total=n; int nonzero=0;
    { static char blk[4096];
      for(int i=0;i<n;i++) if(hdr[i]) {nonzero=1;break;}
      for(;;){int r=s3(SYS_read,fd,(int)blk,(int)sizeof(blk));
              if(r<=0)break; total+=r;
              if(!nonzero)for(int i=0;i<r;i++) if(blk[i]){nonzero=1;break;}}
    }
    s1(SYS_close,fd);
    *size_out=total;
    if(!nonzero) return -6;                    /* 全零文件 */
    return 0;
}

static void work(void)
{
    static char man[2048], out[3072]; int op=0;
    LP=0;
    L("GUARD_START\r\n");

    s2(SYS_mkdir,(int)DIR,0755);

    /* ① 安全模式 */
    if(exists(SAFEMODE)){
        L("SAFEMODE_PRESENT -> refuse to arm\r\n");
        s1(SYS_unlink,(int)BINDF);
        L("bind.txt removed\r\nGUARD_RESULT=REFUSED_SAFEMODE\r\n");
        goto done;
    }

    /* ② 读清单 */
    int mn=slurp(MANIFEST,man,sizeof(man));
    LK("MANIFEST_BYTES=",mn);
    if(mn<=0){
        L("no manifest -> nothing to arm\r\nGUARD_RESULT=NO_MANIFEST\r\n");
        goto done;
    }

    /* ③ 逐行预检，同时写 bind.txt */
    int bfd=s3(SYS_open,(int)BINDF,0x241,0644);
    LK("BINDF_FD=",bfd);
    if(bfd<0){ L("cannot create bind.txt\r\nGUARD_RESULT=ERR_BINDF\r\n"); goto done; }

    int lines=0, okc=0, badc=0; long btp=0;
    { char *p=man;
      while(*p){
          char *e=p; while(*e&&*e!='\n'&&*e!='\r')e++;
          char save=*e; *e=0;
          if(*p){
              char *tab=0; for(char*q=p;*q;q++) if(*q=='\t'){tab=q;break;}
              if(!tab){ LK("LINE_NO_TAB=",(long)lines); badc++; }
              else{
                  *tab=0; char *src=p, *dst=tab+1;
                  long sz=0; long rc=precheck(src,&sz);
                  lines++;
                  L("CHECK "); L(src); L(" -> "); L(dst);
                  LK(" rc=",rc);
                  LK("   size=",sz);
                  if(rc==0){
                      /* 写入 bind.txt: 源<TAB>目标\n */
                      int q=0;
                      while(*src&&q<1024) out[op+q]=*src,src++,q++;
                      out[op+q++]='\t';
                      while(*dst&&q<2000) out[op+q]=*dst,dst++,q++;
                      out[op+q++]='\n';
                      s3(SYS_write,bfd,(int)out,q);
                      btp+=sz; okc++;
                  } else badc++;
              }
          }
          *e=save;
          if(!save)break;
          p=e+1;
          if(*p=='\n')p++;
      }
    }
    s1(SYS_close,bfd);

    LK("LINES=",lines); LK("PASSED=",okc); LK("FAILED=",badc); LK("TOTAL_BYTES=",btp);

    if(badc>0 || okc==0){
        /* ★ 任何一项不过 ⇒ 不武装 */
        s1(SYS_unlink,(int)BINDF);
        L("PRECHECK_FAILED -> bind.txt removed, NOT armed\r\n");
        L("GUARD_RESULT=REFUSED_PRECHECK\r\n");
    } else {
        L("PRECHECK_OK -> armed (agent_v14 will apply bind + watchdog)\r\n");
        L("GUARD_RESULT=ARMED\r\n");
    }

done:
    /* 写日志（guard.log 可能分区不可写，失败就只留 probe.txt） */
    { int f=s3(SYS_open,(int)GUARDLOG,0x241,0644);
      if(f>=0){int o=0;while(o<LP){int r=s3(SYS_write,f,(int)(LOG+o),LP-o);if(r<=0)break;o+=r;}s1(SYS_close,f);} }
    { int f=s3(SYS_open,(int)PROBEOUT,0x241,0644);
      if(f>=0){int o=0;while(o<LP){int r=s3(SYS_write,f,(int)(LOG+o),LP-o);if(r<=0)break;o+=r;}s1(SYS_close,f);} }
}

__attribute__((constructor)) static void init(void){
    int pid=s0(SYS_fork);
    if(pid==0){ work(); s1(SYS_exit,0); for(;;){} }
}
