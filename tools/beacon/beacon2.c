/* beacon2.so —— 自包含取证信标
 * 只做裸 syscall：open/read/write/close/getpid，不 hook、不 exit、不改宿主行为
 * 关键：/tmp 在 /usr 之前挂载 ⇒ 即使 /usr 挂不上，这里的证据也能留到下次开机读取
 * 记录：pid、argv、/usr 挂载行、bootin.elf 首 4 字节、MemFree
 */
#define SYS_write 4
#define SYS_open  5
#define SYS_close 6
#define SYS_read  3
#define SYS_getpid 20

/* r7 必须在 asm 内部装载并列入 clobber（详见 ROUND23_CRITICAL_R7_BUG.md） */
static long sy(long n,long a,long b,long c){
    register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b;
    register long r2 __asm__("r2")=c;
    __asm__ volatile(
        "mov r7, %[nr]\n\t"
        "svc 0"
        : "+r"(r0)
        : [nr]"r"(n), "r"(r1), "r"(r2)
        : "r7","memory");
    return r0;
}
static int slen(const char*s){int n=0;if(!s)return 0;while(s[n])n++;return n;}
static void w(int fd,const char*s,int n){if(fd>=0&&n>0)sy(SYS_write,fd,(long)s,n);}
static void ws(int fd,const char*s){w(fd,s,slen(s));}
static void wd(int fd,long v){                       /* 无除法 */
    char b[16]; int n=0,started=0;
    static const long pw[10]={1000000000L,100000000L,10000000L,1000000L,100000L,10000L,1000L,100L,10L,1L};
    if(v<0){ws(fd,"-");v=-v;}
    if(v==0){ws(fd,"0");return;}
    for(int i=0;i<10;i++){int d=0;while(v>=pw[i]){v-=pw[i];d++;if(d>9)break;}
        if(d||started){b[n++]='0'+d;started=1;}}
    w(fd,b,n);
}
/* 把 /proc 文件里第一个匹配 key 的行写出去 */
static void dump_line(int fd,const char*path,const char*key){
    long f=sy(SYS_open,(long)path,0,0);
    if(f<0){ws(fd,"(open fail) ");ws(fd,path);ws(fd,"\n");return;}
    char buf[2048]; long n=sy(SYS_read,f,(long)buf,sizeof buf-1);
    sy(SYS_close,f,0,0);
    if(n<=0){ws(fd,"(empty) ");ws(fd,path);ws(fd,"\n");return;}
    buf[n]=0;
    int kl=slen(key);
    for(long i=0;i<n;i++){
        if(i+kl<=n){
            int ok=1;
            for(int k=0;k<kl;k++) if(buf[i+k]!=key[k]){ok=0;break;}
            if(ok){
                long j=i; while(j<n && buf[j]!='\n') j++;
                w(fd,buf+i,(int)(j-i)); ws(fd,"\n");
                return;
            }
        }
    }
    ws(fd,"(no match ");ws(fd,key);ws(fd,")\n");
}
static void probe(const char*path){
    long fd=sy(SYS_open,(long)path,1089,0666);       /* O_WRONLY|O_CREAT|O_APPEND */
    if(fd<0) return;
    ws(fd,"=== BEACON pid="); wd(fd,sy(SYS_getpid,0,0,0)); ws(fd,"\n");
    ws(fd,"argv: ");
    long cf=sy(SYS_open,(long)"/proc/self/cmdline",0,0);
    if(cf>=0){char b[512];long n=sy(SYS_read,cf,(long)b,sizeof b-1);
        if(n>0){for(long i=0;i<n;i++) if(b[i]==0) b[i]=' '; w(fd,b,(int)n);} sy(SYS_close,cf,0,0);}
    ws(fd,"\nusr_mount: "); dump_line(fd,"/proc/mounts","/usr ");
    ws(fd,"bootin_head: ");
    long bf=sy(SYS_open,(long)"/usr/bin/bootin.elf",0,0);
    if(bf>=0){char h[8];long hn=sy(SYS_read,bf,(long)h,4);sy(SYS_close,bf,0,0);
        if(hn==4){
            if(h[0]==0x7f&&h[1]=='E'&&h[2]=='L'&&h[3]=='F') ws(fd,"ELF(原厂二进制)\n");
            else if(h[0]=='#'&&h[1]=='!') ws(fd,"SCRIPT(我们的包装)\n");
            else ws(fd,"OTHER\n");
        } else ws(fd,"(read fail)\n");
    } else ws(fd,"(open fail)\n");
    ws(fd,"memfree: "); dump_line(fd,"/proc/meminfo","MemFree");
    ws(fd,"---\n");
    sy(SYS_close,fd,0,0);
}
__attribute__((constructor)) static void go(void){
    probe("/tmp/.beacon.txt");     /* /tmp 在 /usr 之前挂载 ⇒ 最可靠 */
    probe("/log/.beacon.txt");     /* /log 在 /usr 之后，可能不存在 */
}
