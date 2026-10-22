#define SW 4
#define SO 5
#define SR 3
#define SI 6
#define SD 217
static long w(int fd,const void*b,unsigned n){register long r7 __asm__("r7")=SW;register long r0 __asm__("r0")=fd;register long r1 __asm__("r1")=(long)b;register long r2 __asm__("r2")=n;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long op(const char*p,int fl){register long r7 __asm__("r7")=SO;register long r0 __asm__("r0")=(long)p;register long r1 __asm__("r1")=fl;register long r2 __asm__("r2")=0;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long rd(int fd,void*b,unsigned n){register long r7 __asm__("r7")=SR;register long r0 __asm__("r0")=fd;register long r1 __asm__("r1")=(long)b;register long r2 __asm__("r2")=n;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long cl(int fd){register long r7 __asm__("r7")=SI;register long r0 __asm__("r0")=fd;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static long gd(int fd,void*b,unsigned n){register long r7 __asm__("r7")=SD;register long r0 __asm__("r0")=fd;register long r1 __asm__("r1")=(long)b;register long r2 __asm__("r2")=n;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static int c=-1;
static void ps(const char*s){int n=0;while(s[n])n++;w(c,s,n);}
static void pd(long v){char t[12];int i=0;if(v<0){ps("-");v=-v;}if(v==0){ps("0");return;}while(v>0){t[i++]='0'+v%10;v/=10;}for(int j=i-1;j>=0;j--){char s[2]={t[j],0};ps(s);}}
static void sa(char*d,int*i,const char*s){while(*s)d[(*i)++]=*s++;}
extern void*dlsym(void*,const char*) __attribute__((weak));typedef void*(*D)(const char*,int);static D rdl;static int launched;typedef unsigned int(*Sleep)(unsigned int);static Sleep sl;
static long sd(int pol,int prio){static int sp;sp=prio;register long r7 __asm__("r7")=156;register long r0 __asm__("r0")=0;register long r1 __asm__("r1")=pol;register long r2 __asm__("r2")=(long)&sp;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static unsigned long atou(const char**pp){const char*p=*pp;while(*p==' '||*p=='\t')p++;unsigned long v=0;while(*p>='0'&&*p<='9'){v=v*10+(*p-'0');p++;}*pp=p;return v;}
static void statcpu(unsigned long*o){o[0]=o[1]=0;int fd=op("/proc/stat",0);if(fd<0)return;static char b[2048];long n=rd(fd,b,sizeof(b)-1);cl(fd);if(n<=0)return;b[n]=0;const char*p=b;if(p[0]!='c'||p[1]!='p'||p[2]!='u'||p[3]!=' ')return;p+=4;unsigned long u=atou(&p),ni=atou(&p),sy=atou(&p),id=atou(&p),io=atou(&p),ir=atou(&p),so=atou(&p),st=atou(&p);o[0]=u+ni+sy+id+io+ir+so+st;o[1]=id+io;}
static void tasks(void){int fd=op("/proc/self/task",0x10000);if(fd<0){ps("C nodir\n");return;}static char b[4096];long n=gd(fd,b,sizeof(b));cl(fd);if(n<=0)return;long off=0;while(off<n){unsigned short rl=*(unsigned short*)(b+off+16);const char*nm=b+off+19;if(nm[0]!='.'&&rl>19){char path[96];int i=0;sa(path,&i,"/proc/self/task/");sa(path,&i,nm);sa(path,&i,"/stat");int f2=op(path,0);if(f2>=0){static char sb[512];long m=rd(f2,sb,sizeof(sb)-1);cl(f2);if(m>0){sb[m]=0;const char*q=sb;while(*q&&*q!='(')q++;const char*cm=q+1;while(*q&&*q!=')')q++;int ln=0;const char*x=cm;while(x<q&&ln<15){ln++;x++;}const char*p=q;if(*p==')')p++;while(*p==' ')p++;p++; /* state */ for(int k=0;k<10;k++){atou(&p);} unsigned long ut=atou(&p),stt=atou(&p);ps("C tk ");char nb[16];int k2=0;const char*y=cm;while(y<q&&k2<15)nb[k2++]=*y++;nb[k2]=0;ps(nb);ps(" ");pd((long)(ut+stt));ps("\n");(void)ut;(void)stt;}}}off+=rl;}}
unsigned Backup_read_4Byte(unsigned key){
  static unsigned(*real)(unsigned);static int tried;
  if(!tried){tried=1;if(dlsym)real=(unsigned(*)(unsigned))dlsym((void*)-1,"Backup_read_4Byte");}
  if(key==0x00fa00a3u)return 0x20312000u;
  if(key==0x00fa00a4u)return 0x100000u;
  /* MonDrv::Init (libMWF): offset/size for mmap(/dev/mem) of the monitor buffer */
  if(key==0x00fa0028u)return 0x3e000000u;   /* offset */
  if(key==0x00fa005du)return 0x00080000u;   /* size 512KB */
  return real?real(key):0;
}
/* ---- c215: dual inline hooks with entry+exit logging ---- */
static void phx(unsigned v);
extern void hook_a_entry(void); extern unsigned long hook_a_slot;
extern void hook_b_entry(void); extern unsigned long hook_b_slot;
static int a_in_n,a_out_n,b_in_n,b_out_n;
static volatile int g_bmp_win; static int g_win_n, g_glob_n;
void hook_log_a_in(unsigned* s){ g_bmp_win=1; if(a_in_n<5||(a_in_n%50)==0){ ps("HBMP_IN a0=");phx(s[0]);ps(" a1=");phx(s[1]);ps(" a2=");phx(s[2]);ps(" a3=");phx(s[3]);ps(" lr=");phx(s[5]);ps("\n"); } a_in_n++; }
void hook_log_a_out(unsigned ret){ g_bmp_win=0; if(a_out_n<5||(a_out_n%50)==0){ ps("HBMP_OUT ret=");phx(ret);ps("\n"); } a_out_n++; }
void hook_log_b_in(unsigned* s){ if(b_in_n<5||(b_in_n%50)==0){ ps("E58_IN ctx=");phx(s[0]);ps(" c16=");phx(*(volatile unsigned*)(s[0]+16));ps(" lock=");phx(*(volatile unsigned*)(s[0]+0x58));ps(" r1=");phx(s[1]);ps(" r2=");phx(s[2]);ps(" r3=");phx(s[3]);ps(" lr=");phx(s[5]);ps("\n"); } b_in_n++; }
void hook_log_b_out(unsigned ret){ if(b_out_n<5||(b_out_n%50)==0){ ps("E58_OUT ret=");phx(ret);ps("\n"); } b_out_n++; }
static long sys_mp5(unsigned long a,unsigned l,int p){ register long r7 __asm__("r7")=125; register long r0 __asm__("r0")=(long)a; register long r1 __asm__("r1")=(long)l; register long r2 __asm__("r2")=p; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory"); return r0; }
static long sys_mm5(unsigned long a,unsigned l,int p,int f,int fd,unsigned o){ register long r7 __asm__("r7")=192; register long r0 __asm__("r0")=(long)a; register long r1 __asm__("r1")=(long)l; register long r2 __asm__("r2")=p; register long r3 __asm__("r3")=f; register long r4 __asm__("r4")=fd; register long r5 __asm__("r5")=o; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory"); return r0; }
static int install_hook5(unsigned long base,unsigned long off,void*entry,unsigned long*slot){
  unsigned long t=base+off; unsigned long pg=t&~0xFFFUL; unsigned long spg=((unsigned long)entry)&~0xFFFUL;
  if(sys_mp5(pg,0x1000,7)!=0){ ps("H5 mp1 fail\n"); return -1; }
  sys_mp5(spg,0x1000,7);
  unsigned long trp=(unsigned long)sys_mm5(0,0x1000,7,0x22,-1,0);
  if((long)trp<0){ ps("H5 mm fail\n"); return -1; }
  unsigned char* s=(unsigned char*)t; unsigned char* d=(unsigned char*)trp;
  for(int i=0;i<8;i++) d[i]=s[i];
  d[8]=0xdf;d[9]=0xf8;d[10]=0x00;d[11]=0xf0; unsigned long back=(t+8)|1UL; for(int i=0;i<4;i++) d[12+i]=(back>>(8*i))&0xff;
  *slot=trp|1UL;
  unsigned char* n=(unsigned char*)t; n[0]=0xdf;n[1]=0xf8;n[2]=0x00;n[3]=0xf0; unsigned long st=(unsigned long)entry|1UL; for(int i=0;i<4;i++) n[4+i]=(st>>(8*i))&0xff;
  __builtin___clear_cache((char*)t,(char*)(t+8)); __builtin___clear_cache((char*)trp,(char*)(trp+16));
  sys_mp5(pg,0x1000,5);
  ps("H5 ok off=");phx((unsigned)off);ps(" trp=");phx((unsigned)trp);ps("\n"); return 0;
}
static void install_all5(unsigned long base){
  if(install_hook5(base,0x69CA08UL,(void*)&hook_a_entry,&hook_a_slot)!=0) ps("H5 A fail\n");
  if(install_hook5(base,0x7685A8UL,(void*)&hook_b_entry,&hook_b_slot)!=0) ps("H5 B fail\n");
}

static int fix_n;
static void sync_win_log(unsigned qid,void*msg,unsigned len,unsigned long lr,int ret,unsigned* pre){
  int inwin=0;
  /* c229: full dump of the grm_ma bitmap-create message (w2==0xE1000 && w3==0x280) */
  if(qid==0x014c0424U && msg){
    unsigned long p=(unsigned long)msg;
    if((p&3UL)==0UL && p>=0x04000000UL && p<0x0C000000UL){
      volatile unsigned*mm=(volatile unsigned*)msg;
      if((mm[0]&0xffffU)==9U && mm[2]==0x000E1000U && mm[3]==0x280U){
        if(fix_n<4){
          fix_n++;
          ps("BM "); phx((unsigned)p);
          for(int k=0;k<11;k++){ ps(" "); phx(mm[k]); }
          ps("\n");
          unsigned long w1=(unsigned long)mm[1];
          if(w1>=0x04000000UL && w1<0x0C000000UL){
            volatile unsigned*r=(volatile unsigned*)w1;
            ps("BR "); phx((unsigned)w1);
            for(int k=0;k<12;k++){ ps(" "); phx(r[k]); }
            ps("\n");
          }
          mm[4]=0; mm[6]=0; mm[7]=0;
          ps("BMSET w4/w6/w7=0 -> "); phx(mm[4]); ps("\n");
          ps("BMret "); phx((unsigned)ret); ps("\n");
        }
      }
    }
  }
  if(inwin && g_win_n<40){ g_win_n++;
    ps("BMPWIN qid=");phx(qid);ps(" len=");phx(len);ps(" msg=");phx((unsigned)msg);ps(" lr=");phx((unsigned)lr);ps(" ret=");phx((unsigned)ret);
    volatile unsigned*w=(volatile unsigned*)msg; ps(" pre=");phx(pre?pre[0]:0);phx(pre?pre[1]:0);phx(pre?pre[2]:0);phx(pre?pre[3]:0);
    if(msg){ ps(" post=");phx(w[0]);phx(w[1]);phx(w[2]);phx(w[3]); } ps("\n");
    return; }
  if(!inwin && g_glob_n<40){ g_glob_n++; ps("QWIN qid=");phx(qid);ps(" len=");phx(len);ps(" msg=");phx((unsigned)msg);ps(" lr=");phx((unsigned)lr);ps(" ret=");phx((unsigned)ret);
    volatile unsigned*w=(volatile unsigned*)msg; if(msg){ ps(" w=");phx(w[0]);phx(w[1]);phx(w[2]);phx(w[3]); } ps("\n"); }
}

void abort_dump(unsigned long*sp){ps("AAB3 lr=");pd((long)sp[8]);ps("\n");for(int i=0;i<64;i++){unsigned long v=sp[i];if(v>=0x04000000UL&&v<0x60000000UL&&(v&1UL)){ps("AAB3 [");pd(i);ps("]=");pd((long)v);ps("\n");}}for(;;){struct zzts2{long s,ns;};struct zzts2 zt2; zt2.s=0; zt2.ns=300000000; register long r7 __asm__("r7")=162; register long r0 __asm__("r0")=(long)&zt2; register long r1 __asm__("r1")=0; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory"); }}
__attribute__((naked)) void abort(void){__asm__ volatile("push {r4-r11,lr}\n mov r0, sp\n b abort_dump\n");}
void __assert_fail(const char*a,const char*f,unsigned l,const char*fn){(void)fn;ps("AAS ");ps(a?a:"?");ps(" @");ps(f?f:"?");ps(":");pd((long)l);ps("\n");for(;;){}}
void __stack_chk_fail(void){ps("AASC\n");for(;;){}}
static void sysrq_t(void){ int fd=op("/proc/sysrq-trigger",1); if(fd>=0){ w(fd,"t",1); cl(fd);} }
static volatile int in2_done;
static volatile int ida_cnt;

static unsigned long g_libbase, g_rx, g_rw; static volatile int ia_done; static volatile int ida_cnt2;
static int (*g_realadd)(void*,int,const char*,const char*);
long _ZN9IdSoTable3addEiPKcS1_(void*th,int id,const char*a,const char*b){
  if(!g_realadd) g_realadd=(int(*)(void*,int,const char*,const char*))(g_libbase+0x8a20a8UL|1UL);
  ida_cnt2++;
  return g_realadd?g_realadd(th,id,a,b):0;
}

static unsigned hexv9(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 99; }
static int sfind(const char*h,const char*n){ for(int i=0;h[i];i++){ int j=0; while(n[j]&&h[i+j]==n[j])j++; if(!n[j])return 1; } return 0; }
static void sysrq_t2(void){ int fd=op("/proc/sysrq-trigger",1); if(fd>=0){ w(fd,"t",1); cl(fd);} }
static void phx(unsigned v){ char b[11]; const char*hx="0123456789abcdef"; b[0]='0';b[1]='x'; for(int i=0;i<8;i++){ b[2+i]=hx[(v>>(28-4*i))&15]; } b[10]=0; ps(b); }
static unsigned au_q[64]; static int au_n[64]; static int au_c; static int autofake_n;
typedef int(*SMSX)(unsigned,void*,unsigned,unsigned);
typedef struct { unsigned qid; int n,r0,r1020,r1024,ro; unsigned long lrf,lrl; } QST;
#define QMAX 96
static QST st_s[QMAX]; static int st_sn; static QST st_a[QMAX]; static int st_an;
static int raw_s, raw_a; static int qfake_n;
static void qrec(QST*t,int*n,unsigned qid,int ret,unsigned long lr){
  int i; for(i=0;i<*n;i++) if(t[i].qid==qid) break;
  if(i==*n && *n<QMAX){ t[i].qid=qid; t[i].n=0;t[i].r0=0;t[i].r1020=0;t[i].r1024=0;t[i].ro=0;t[i].lrf=lr;t[i].lrl=lr; (*n)++; }
  if(i<*n){ t[i].n++; if(ret==0)t[i].r0++; else if(ret==-1020)t[i].r1020++; else if(ret==-1024)t[i].r1024++; else t[i].ro++; t[i].lrl=lr; }
}
static SMSX r_ssmx, r_smx;
unsigned osal_snd_sync_msg(unsigned qid,void*msg,unsigned len,unsigned x4){
  unsigned long lr=(unsigned long)__builtin_return_address(0); int ret;
  if(qid==0x008f013aU){ ret=0; }
  else {
    if(!r_ssmx) r_ssmx=(SMSX)dlsym((void*)-1,"osal_snd_sync_msg");
    unsigned pre4[4]={0,0,0,0}; if(msg){ volatile unsigned*pp=(volatile unsigned*)msg; pre4[0]=pp[0];pre4[1]=pp[1];pre4[2]=pp[2];pre4[3]=pp[3]; }
    ret=r_ssmx?r_ssmx(qid,msg,len,x4):-1;
    sync_win_log(qid,msg,len,lr,ret,pre4);
    if(ret==-1020){
      if(autofake_n<40){ autofake_n++; ps("AUTOFAKE qid="); phx(qid); ps(" len="); pd(len); unsigned*w=(unsigned*)msg; ps(" w0="); phx(w?w[0]:0); ps(" w1="); phx(w?w[1]:0); ps(" lr="); phx((unsigned)lr); ps("\n"); }
      int f=-1; for(int i=0;i<au_c;i++) if(au_q[i]==(unsigned)qid){ f=i; break; }
      if(f<0 && au_c<64){ au_q[au_c]=(unsigned)qid; au_n[au_c]=0; f=au_c; au_c++; }
      if(f>=0) au_n[f]++;
      ret=0;
    }
  }
  qrec(st_s,&st_sn,qid,ret,lr);
  if(raw_s<30){ raw_s++; ps("QS raw qid=");pd(qid);ps(" ret=");pd(ret);ps(" lr=");pd((long)lr);ps("\n"); }
  return ret;
}
unsigned osal_snd_msg(unsigned qid,void*msg,unsigned len,unsigned x4){
  unsigned long lr=(unsigned long)__builtin_return_address(0); int ret;
  if(!r_smx) r_smx=(SMSX)dlsym((void*)-1,"osal_snd_msg"); ret=r_smx?r_smx(qid,msg,len,x4):-1;
  qrec(st_a,&st_an,qid,ret,lr);
  if(raw_a<30){ raw_a++; ps("QA raw qid=");pd(qid);ps(" ret=");pd(ret);ps(" lr=");pd((long)lr);ps("\n"); }
  return ret;
}
static void qdump(void){
  for(int i=0;i<au_c;i++){ ps("QAUTO qid=");phx(au_q[i]);ps(" n=");pd(au_n[i]);ps("\n"); }
  for(int i=0;i<st_sn;i++){ ps("QSYNC qid=");pd((long)st_s[i].qid);ps(" n=");pd(st_s[i].n);ps(" ret0=");pd(st_s[i].r0);ps(" retM1020=");pd(st_s[i].r1020);ps(" retM1024=");pd(st_s[i].r1024);ps(" retO=");pd(st_s[i].ro);ps(" lrf=");pd((long)st_s[i].lrf);ps(" lrl=");pd((long)st_s[i].lrl);ps("\n"); }
  for(int i=0;i<st_an;i++){ ps("QASYNC qid=");pd((long)st_a[i].qid);ps(" n=");pd(st_a[i].n);ps(" ret0=");pd(st_a[i].r0);ps(" retM1020=");pd(st_a[i].r1020);ps(" retM1024=");pd(st_a[i].r1024);ps(" retO=");pd(st_a[i].ro);ps(" lrf=");pd((long)st_a[i].lrf);ps(" lrl=");pd((long)st_a[i].lrl);ps("\n"); }
}
static void*worker(void*x){sd(1,90);if(sl)sl(8);ps("Y9 worker\n");if(!dlsym)return 0;typedef void*(*AI)(void*);AI ai=(AI)dlsym((void*)0,"Application_System_Init");void*(*gf)(void)=(void*(*)(void))dlsym((void*)0,"_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");void*(*rf)(void)=(void*(*)(void))dlsym((void*)0,"_ZN2ux6resmgr15ResourceManager15sys_getInstanceEv");void*(*wf)(void)=(void*(*)(void))dlsym((void*)0,"_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");unsigned long base=0;if(ai)base=((unsigned long)ai - 0x8a2408UL)&~1UL;ps("Y9 base=");pd((long)base);ps("\n");g_libbase=base&~1UL;void*(*g_idget)(const char*)=(void*(*)(const char*))dlsym((void*)0,"_ZN11IdGenerator3GetEPKc");ps("IDGET ");pd((long)g_idget);ps("\n");
if(base){int(*rmi)(void)=(int(*)(void))(base+0x7740e0UL|1UL);ps("Y9 rm_init...\n");int rr=rmi();ps("Y9 rm_init=");pd(rr);ps("\n");}
unsigned long bb=0;if(ai)bb=((unsigned long)ai&~1UL)-0x8a2408UL;
if(ai){ps("Y9 appinit\n");ai(0);ps("Y9 returned\n");}for(int i=0;i<60;i++){if(i==56)qdump();if(i==40&&base&&!ia_done){ ia_done=1; unsigned long bb=base&~1UL; ps("W0 start\n"); { static char mb[40000]; int fd=op("/proc/self/maps",0); long o=0; if(fd>=0){ while(o<(long)sizeof(mb)-1){ long r=rd(fd,mb+o,sizeof(mb)-1-o); if(r<=0)break; o+=r; } cl(fd); } mb[o>0?o:0]=0; ps("W0 maps len="); pd(o); ps("\n"); unsigned long rw=0; char*ln=mb; while(*ln){ char*e=ln; while(*e&&*e!='\n')e++; char cv=*e; *e=0; if(sfind(ln,"viewUnified2")){ ps("W1 "); ps(ln); ps("\n"); if(rw==0){ const char*p=ln; unsigned long st=0,en=0,off=0; unsigned d; while((d=hexv9(*p))<16){ st=st*16+d; p++; } if(*p=='-')p++; while((d=hexv9(*p))<16){ en=en*16+d; p++; } while(*p==' ')p++; if(p[0]=='r'&&p[1]=='w'){ p+=4; while(*p==' ')p++; while((d=hexv9(*p))<16){ off=off*16+d; p++; } rw=st; ps("W2 rwstart="); pd((long)rw); ps(" off="); pd((long)off); ps("\n"); } } } *e=cv; if(!cv)break; ln=e+1; } if(rw){ unsigned long rx=rw-0xB80000UL; ps("W3 rx="); pd((long)rx); ps("\n"); unsigned long arr=rx+0xB80AF0UL; unsigned long init=(rx+0x185CC8UL)|1UL; ps("W3 init="); pd((long)init); ps(" arr="); pd((long)arr); ps("\n"); for(int k=0;k<4;k++){ unsigned fp=*(volatile unsigned*)(arr+4*k); ps("W4 k="); pd(k); ps(" fp="); pd(fp); ps("\n"); } { void(*fn)(void)=(void(*)(void))init; ps("W5 call init\n"); fn(); ps("W5 init returned\n"); } int n=1768; for(int k=0;k<n;k++){ unsigned fp=*(volatile unsigned*)(arr+4*k); if(k<8){ ps("IA k="); pd(k); ps(" fp="); pd(fp); ps("\n"); } if(!fp) continue; { void(*cf)(void)=(void(*)(void))(fp|1UL); cf(); } if((k%100)==0){ ps("IA prog k="); pd(k); ps(" ida="); pd(ida_cnt2); ps("\n"); } } ps("IA done ida="); pd(ida_cnt2); ps("\n"); } else ps("W3 no rw line\n"); } void*pvm=*(void**)(bb+0x149221cUL); ps("W6 pvm="); pd((long)pvm); ps("\n"); if(pvm){ *(volatile unsigned*)(bb+0x145977cUL)=(unsigned)(unsigned long)pvm; typedef int(*OV)(const char*,int); OV ov=(OV)(bb+0x14117cUL|1UL); int rr=ov("view/SCALAR",0); ps("W6 ovret="); pd(rr); ps("\n"); } }
if(base&&(i%4)==0){volatile unsigned*pv=(volatile unsigned*)((base&~1UL)+0x149221cUL);volatile unsigned*iv=(volatile unsigned*)((base&~1UL)+0x1459548UL);ps("PV i=");pd(i);ps(" pVM=");pd(pv[0]);ps(" inst=");pd(iv[0]);ps(" ida=");pd(ida_cnt);ps("\n");}
ps("Y9 i=");pd(i);ps(" gfx=");pd((long)(gf?(unsigned long)gf():0));ps(" res=");pd((long)(rf?(unsigned long)rf():0));ps(" wg=");pd((long)(wf?(unsigned long)wf():0));if(bb){volatile int*w=(volatile int*)(bb+0x145df44UL);ps(" w0=");pd(w[0]);ps(" w2=");pd(w[2]);}ps("\n");if(sl)sl(1);}return 0;}

static int(*r_bnread)(unsigned,void*,unsigned);

static void nsleep_ms(long ms){struct tsp{ long s,ns;}; struct tsp ts; ts.s=0; ts.ns=ms*1000000L; register long r7 __asm__("r7")=162; register long r0 __asm__("r0")=(long)&ts; register long r1 __asm__("r1")=0; __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1):"memory");}
#define RETRY_OK(r) ((r)!=-1020 && (r)!=-1024)
/* NOTE: do NOT interpose Backup_read/Backup_get_datasize: libbackup internally
   calls them through its own PLT, so a wrapper recurses infinitely (stack
   overflow: fault at guard page, pc in wrapper, lr in libbackup). */


static unsigned char *bnr_scratch;
static int(*r_breadX)(int,void*);
static int(*r_bdsX)(int);

static unsigned long *g_maps; static int g_maps_n;
static void load_maps(void){
  if(g_maps_n) return;
  if(!g_maps){ static void*(*rmp)(unsigned long); if(!rmp&&dlsym)rmp=(void*(*)(unsigned long))dlsym((void*)-1,"malloc"); if(!rmp)return; g_maps=(unsigned long*)rmp(0x800); }
  char*buf; static void*(*rmp2)(unsigned long); if(!rmp2&&dlsym)rmp2=(void*(*)(unsigned long))dlsym((void*)-1,"malloc"); if(!rmp2)return; buf=(char*)rmp2(0x10000);
  if(!g_maps||!buf)return;
  int fd=op("/proc/self/maps",0); if(fd<0)return;
  int n=(int)rd(fd,buf,0xFFFF); cl(fd); if(n<=0)return; buf[n]=0;
  char*p=buf;
  while(*p && g_maps_n<170){
    unsigned long st=0,en=0;
    while(*p && *p!='-'){int c=*p; unsigned d=(c<='9')?(unsigned)(c-'0'):(unsigned)((c|32)-'a'+10); st=st*16+d; p++;}
    if(*p=='-')p++;
    while(*p && *p!=' '){int c=*p; unsigned d=(c<='9')?(unsigned)(c-'0'):(unsigned)((c|32)-'a'+10); en=en*16+d; p++;}
    while(*p==' ')p++;
    int w=0; if(p[0]=='r'&&(p[1]=='w')) w=1; else if(p[0]=='r'&&p[1]=='-'&&p[2]=='x') w=0; else w=0;
    while(*p && *p!='\n')p++; if(*p)p++;
    if(en>st){ g_maps[g_maps_n*3]=st; g_maps[g_maps_n*3+1]=en; g_maps[g_maps_n*3+2]=(unsigned long)w; g_maps_n++; }
  }
  { static void(*rmf)(void*); if(!rmf&&dlsym)rmf=(void(*)(void*))dlsym((void*)-1,"free"); if(rmf)rmf(buf); }
}
static int copy_ok(unsigned long b,unsigned n){
  load_maps();
  if(!g_maps_n) return 1;
  for(int i=0;i<g_maps_n;i++){
    unsigned long s=g_maps[i*3],e=g_maps[i*3+1]; int w=(int)g_maps[i*3+2];
    if(b>=s&&b<e) return (w!=0)&&((b+(unsigned long)n)<=e);
  }
  return 0;
}

int Backup_read(int id,void*buf){
  if(!r_breadX&&dlsym)r_breadX=(int(*)(int,void*))dlsym((void*)-1,"Backup_read");
  if(!r_bdsX&&dlsym)r_bdsX=(int(*)(int))dlsym((void*)-1,"Backup_get_datasize");
  if(!r_breadX)return -1;
  if((id&0xfffffff0u)==0x11b0000u) return 0;           /* InfraDial bogus ids */
  if(!bnr_scratch){ static void*(*rmx)(unsigned long); if(!rmx&&dlsym)rmx=(void*(*)(unsigned long))dlsym((void*)-1,"malloc"); if(rmx)bnr_scratch=(unsigned char*)rmx(0x18000); if(!bnr_scratch)return r_breadX(id,buf); }
  if(id==0x01190001u||id==0x01190000u){
    int rr=r_breadX(id,bnr_scratch);
    if(buf){ if(rr==1) ((unsigned char*)buf)[0]=((unsigned char*)bnr_scratch)[0]; else ((unsigned char*)buf)[0]=1u; }
    return 1;
  }
  int d=r_bdsX?r_bdsX(id):-1;
  int r=r_breadX(id,bnr_scratch);
  unsigned n=(d>0)?(unsigned)d:0u; if(n>0x18000u)n=0x18000u; if(!buf)n=0;
  if(n>4096u && (unsigned long)buf>=0x08000000UL && (unsigned long)buf<0x0a000000UL) return r; /* stack guard */
  if(!copy_ok((unsigned long)buf,n)){ ps("BRDSKIP read id=0x");{char t[12];int i7=0;unsigned v=id;char b7[10];int j7=0;if(!v){b7[j7++]='0';}while(v){unsigned dd=v&15;b7[j7++]="0123456789abcdef"[dd];v>>=4;}while(j7>0)t[i7++]=b7[--j7];t[i7]=0;ps(t);}ps("\n"); return r; }
  if(!copy_ok((unsigned long)buf,n)){ ps("BRDSKIP nread\n"); return r; }
  { unsigned char*dp=(unsigned char*)buf; for(unsigned k=0;k<n;k++)dp[k]=bnr_scratch[k]; }
  return r;
}

static unsigned char *bnr_scratch;
static long sys_gettid2(void){ register long r7 __asm__("r7")=224; register long r0 __asm__("r0")=0; __asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory"); return r0; }
static volatile int ml_seen;
int Backup_nread(unsigned id,void*buf,unsigned size){
  if(!ml_seen){ ml_seen=1; ps("ML tid="); pd(sys_gettid2()); ps("\n"); }
  if(!r_bnread&&dlsym)r_bnread=(int(*)(unsigned,void*,unsigned))dlsym((void*)-1,"Backup_nread");
  /* keep caller semantics: pass caller size, but receive into a big scratch buffer
     because the kernel driver ignores size and copies the whole record. */
  if(!bnr_scratch){ static void*(*rmalloc2)(unsigned long); if(!rmalloc2&&dlsym)rmalloc2=(void*(*)(unsigned long))dlsym((void*)-1,"malloc"); if(rmalloc2)bnr_scratch=(unsigned char*)rmalloc2(0x18000); if(!bnr_scratch){ps("BNR nomem\n");return -1;} }
  int r=r_bnread?r_bnread(id,bnr_scratch,size):-1;
  for(int i=0;i<200&&!RETRY_OK(r);i++){nsleep_ms(1);r=r_bnread?r_bnread(id,bnr_scratch,size):-1;}
  ps("BNR id=0x");{char t[12];int i2=0;unsigned v=id;char b[10];int j2=0;if(!v){b[j2++]='0';}while(v){unsigned d=v&15;b[j2++]="0123456789abcdef"[d];v>>=4;}while(j2>0)t[i2++]=b[--j2];t[i2]=0;ps(t);}
  ps(" sz=");pd(size);ps(" real=");pd(r);
  if(r<=0||r==0xffff){ps(" passthru\n");return r;}
  unsigned n=size; if((unsigned)r<n)n=(unsigned)r;
  { unsigned char*d=(unsigned char*)buf; for(unsigned k=0;k<n;k++) d[k]=bnr_scratch[k]; }
  ps(" ret=");pd((long)n);ps("\n");
  return (int)n;
}
__attribute__((constructor))static void init(void){c=op("/dev/console",1);if(c<0)c=2;ps("D1 alive\n");}
void*dlopen(const char*n,int f){if(!rdl&&dlsym)rdl=(D)dlsym((void*)-1,"dlopen");void*r=rdl?rdl(n,f):0;if(n&&n[0]=='/'&&n[1]=='u'&&n[2]=='s'&&n[3]=='r'&&n[4]=='/'&&n[5]=='l'&&n[6]=='i'&&n[7]=='b'&&!launched){int i=0;while(n[i])i++;if(i>=9&&n[i-9]=='l'){launched=1;ps("C libObj\n");if(dlsym){sl=(Sleep)dlsym((void*)0,"sleep");typedef int(*PC)(unsigned long*,void*,void*(*)(void*),void*);PC pc=(PC)dlsym((void*)0,"pthread_create");if(pc){unsigned long tid=0;pc(&tid,0,worker,0);ps("C worker started\n");}}}}return r;}
