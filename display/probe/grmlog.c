/* libgrmlog.so v2 —— App 进程内探针
 * 1) GRM_*（绘图边界）调用记录
 * 2) dlopen 记录（看 viewUnified 等是否加载、是否失败）
 * 3) ux::gfxeng::GraphicsEngine::sys_getInstance（UI 引擎初始化）
 * 4) OBJAVBBCBASECLASS::SeqController::CreateAndAddSequence（屏幕/序列创建）
 * 零依赖：用 svc 0 写 /dev/console；对被拦函数用 dlsym(RTLD_NEXT) 调原始实现
 */
#define SYS_write 4
#define SYS_open  5
static long sys_write(int fd, const void *b, unsigned long n)
{ register long r7 __asm__("r7")=SYS_write; register long r0 __asm__("r0")=fd;
  register long r1 __asm__("r1")=(long)b; register long r2 __asm__("r2")=n;
  __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory"); return r0; }
static long sys_open(const char *p,int f,int m)
{ register long r7 __asm__("r7")=SYS_open; register long r0 __asm__("r0")=(long)p;
  register long r1 __asm__("r1")=f; register long r2 __asm__("r2")=m;
  __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory"); return r0; }

extern void *dlsym(void *h, const char *s) __attribute__((weak));
#define RTLD_NEXT ((void *)-1l)

static int con=-1;
static void ps(const char *s){const char*p=s;int n=0;while(p[n])n++;sys_write(con,s,n);}
static void hx(unsigned long v){char b[11];int i;b[0]='0';b[1]='x';
  for(i=0;i<8;i++){int d=(v>>((7-i)*4))&0xF;b[2+i]=d<10?'0'+d:'a'+d-10;}b[10]=0;sys_write(con,b,10);}

static void pstr(const char *s)
{
    int n = 0;
    if (!s) { ps("(null)"); return; }
    while (s[n] && n < 72) n++;
    sys_write(con, s, n);
    if (s[n]) ps("...");
}

static void ev2(const char*n,unsigned long a,unsigned long b){ps(n);ps(" ");hx(a);ps(" ");hx(b);ps("\n");}
static void ev3(const char*n,unsigned long a,unsigned long b,unsigned long c){ps(n);ps(" ");hx(a);ps(" ");hx(b);ps(" ");hx(c);ps("\n");}

__attribute__((constructor)) static void ini(void)
{ con=(int)sys_open("/dev/console",1,0); if(con<0)con=2; ps("GRMLOG v2 alive\n"); }

/* ---- dlopen ---- */
void *dlopen(const char *name, int flag)
{
    static void *(*real)(const char*,int);
    if(!real && dlsym) real=(void*(*)(const char*,int))dlsym(RTLD_NEXT,"dlopen");
    void *r = real ? real(name,flag) : (void*)0;
    ps("GRMLOG dlopen "); pstr(name); ps(" -> "); hx((unsigned long)r); ps("\n");
    return r;
}
void *dlopen64(const char *name, int flag)
{ return dlopen(name,flag); }

/* ---- 图形引擎单例 ---- */
void *_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv(void)
{
    static void *(*real)(void);
    if(!real && dlsym) real=(void*(*)(void))dlsym(RTLD_NEXT,"_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    void *r = real ? real() : (void*)0;
    ev2("GRMLOG GraphicsEngine", (unsigned long)r, 0);
    return r;
}

/* ---- 序列/屏幕创建：id 是关键信息 ---- */
unsigned _ZN17OBJAVBBCBASECLASS13SeqController20CreateAndAddSequenceEjPN3MWF6ObjMsgE(void *self, unsigned id, void *msg)
{
    static unsigned (*real)(void*,unsigned,void*);
    if(!real && dlsym) real=(unsigned(*)(void*,unsigned,void*))dlsym(RTLD_NEXT,
        "_ZN17OBJAVBBCBASECLASS13SeqController20CreateAndAddSequenceEjPN3MWF6ObjMsgE");
    unsigned r = real ? real(self,id,msg) : 0;
    ev3("GRMLOG CreateAndAddSequence id=", (unsigned long)id, (unsigned long)r, (unsigned long)self);
    return r;
}

/* ---- GRM 绘图边界 ---- */
#define GRM_HOOK(nm) \
 long nm(long a,long b,long c,long d,long e,long f) \
 { ev3("GRMLOG " #nm, (unsigned long)a, (unsigned long)b, (unsigned long)c); return 0; }
GRM_HOOK(GRM_gpermFillrect)
GRM_HOOK(GRM_gpermRectblit)
GRM_HOOK(GRM_bitmapCreate)
GRM_HOOK(GRM_bitmapWriteLockBits)
GRM_HOOK(GRM_bitmapWriteUnlockBits)
GRM_HOOK(GRM_screenGetOnBitmap)
GRM_HOOK(GRM_fbrmSetScreenPreference)

/* ---- WiFi 启动入口：只记录并返回成功，看是否解除阻塞 ---- */
int _ZN10WlanAdjust5startEv(void)
{
    static int (*real)(void);
    if(!real) real=(int(*)(void))dlsym(RTLD_NEXT,"_ZN10WlanAdjust5startEv");
    ev2("GRMLOG WlanAdjust::start called", (unsigned long)real, 0);
    return 0;
}
int _ZN10WlanAdjust9wlanStartEP10OsalPacket(void *self, void *pkt)
{
    static int (*real)(void*,void*);
    if(!real) real=(int(*)(void*,void*))dlsym(RTLD_NEXT,"_ZN10WlanAdjust9wlanStartEP10OsalPacket");
    ev3("GRMLOG WlanAdjust::wlanStart", (unsigned long)self, (unsigned long)pkt, (unsigned long)real);
    return 0;
}

/* ---- 等待类探针 ---- */
struct pollfd_ { int fd; short events; short revents; };
int poll(struct pollfd_ *fds, unsigned long n, int timeout)
{
    static int (*real)(struct pollfd_*, unsigned long, int);
    if(!real) real=(int(*)(struct pollfd_*,unsigned long,int))dlsym(RTLD_NEXT,"poll");
    static int cnt; int r = real ? real(fds,n,timeout) : 0;
    if (cnt < 150) { cnt++;
        ps("GRMLOG poll n="); hx(n); ps(" tmo="); hx((unsigned long)timeout);
        ps(" fd0="); hx(fds && n ? (unsigned long)fds[0].fd : 0);
        ps(" ev0="); hx(fds && n ? (unsigned long)fds[0].events : 0);
        ps(" r="); hx((unsigned long)r); ps("\n");
    }
    return r;
}
int select(int nfds, void *rd, void *wr, void *ex, void *tv)
{
    static int (*real)(int,void*,void*,void*,void*);
    if(!real) real=(int(*)(int,void*,void*,void*,void*))dlsym(RTLD_NEXT,"select");
    static int cnt; int r = real ? real(nfds,rd,wr,ex,tv) : 0;
    if (cnt < 60) { cnt++;
        ps("GRMLOG select nfds="); hx((unsigned long)nfds); ps(" r="); hx((unsigned long)r); ps("\n");
    }
    return r;
}
int epoll_wait(int epfd, void *ev, int maxev, int timeout)
{
    static int (*real)(int,void*,int,int);
    if(!real) real=(int(*)(int,void*,int,int))dlsym(RTLD_NEXT,"epoll_wait");
    static int cnt; int r = real ? real(epfd,ev,maxev,timeout) : 0;
    if (cnt < 150) { cnt++;
        ps("GRMLOG epoll epfd="); hx((unsigned long)epfd); ps(" tmo="); hx((unsigned long)timeout);
        ps(" r="); hx((unsigned long)r); ps("\n");
    }
    return r;
}
