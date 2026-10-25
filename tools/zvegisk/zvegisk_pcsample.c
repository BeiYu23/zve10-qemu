/* zvegisk_pcsample.c — 采样 im.elf 各线程的 PC，找出热函数
 *
 * 原理：ptrace 每个线程 → GETREGS 拿 PC → DETACH
 *       重复 N 次，把 PC 映射到 libObj 的偏移，统计直方图
 * ★ 完全只读，不修改任何内存
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define OUT "/log/zvegisk_pc.log"
#define MAXS 8192

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[768];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

/* 统计：PC -> 次数 */
static unsigned long samples[MAXS];
static int nsamp;

static unsigned long lo_base=0, lo_end=0;   /* libObj 代码段范围 */

static void find_range(int pid)
{
    char p[64]; snprintf(p,sizeof p,"/proc/%d/maps",pid);
    FILE*f=fopen(p,"r"); if(!f)return;
    char l[512];
    while(fgets(l,sizeof l,f)){
        if(!strstr(l,"libObj.so")||!strstr(l,"r-xp"))continue;
        unsigned long s=0,e=0;
        if(sscanf(l,"%lx-%lx",&s,&e)==2){ lo_base=s; lo_end=e; logf("libObj r-xp: 0x%lx - 0x%lx",s,e); break; }
    }
    fclose(f);
}

static void sample_thread(int pid, int tid)
{
    if (ptrace(PTRACE_ATTACH, tid, 0, 0) < 0) return;
    int st=0; waitpid(tid,&st,0);
    struct user_regs regs;
    memset(&regs, 0, sizeof regs);
    if (ptrace(PTRACE_GETREGS, tid, 0, &regs) == 0) {
        /* 32-bit ARM: PC = uregs[15] */
        unsigned long pc = (unsigned long)regs.uregs[15];
        if (nsamp < MAXS) samples[nsamp++] = pc;
    }
    ptrace(PTRACE_DETACH, tid, 0, 0);
}

int main(int argc, char **argv)
{
    wrr();
    logf("PCSAMPLE_START uid=%d", geteuid());
    int pid = (argc > 1) ? atoi(argv[1]) : 157;
    int rounds = (argc > 2) ? atoi(argv[2]) : 40;

    find_range(pid);
    if (!lo_base) { logf("NO_LIBOBJ"); logf("PCSAMPLE_DONE"); return 1; }

    /* 枚举线程 */
    int tids[256]; int nt=0;
    { char d[64]; snprintf(d,sizeof d,"/proc/%d/task",pid);
      DIR *dp=opendir(d);
      if(dp){ struct dirent *e;
        while((e=readdir(dp)) && nt<256) if(e->d_name[0]!='.') tids[nt++]=atoi(e->d_name);
        closedir(dp);} }
    logf("threads=%d", nt);
    if (nt==0) { logf("PCSAMPLE_DONE"); return 1; }

    for (int r=0; r<rounds; r++) {
        for (int i=0;i<nt;i++) sample_thread(pid, tids[i]);
        usleep(20000);   /* 20 ms */
    }
    logf("samples=%d", nsamp);

    /* ★ 新增：按【库】统计 PC 分布 */
    {
        struct { char name[64]; unsigned long lo, hi; int cnt; } libs[64]; int nl=0;
        /* 收集所有 userspace 映射 */
        char mp[64]; snprintf(mp,sizeof mp,"/proc/%d/maps",pid);
        FILE *mf=fopen(mp,"r"); char ml[512];
        while(mf && fgets(ml,sizeof ml,mf)){
            unsigned long a=0,b=0; char perm[8]={0}; char path[256]={0};
            if(sscanf(ml,"%lx-%lx %7s %*s %*s %*s %255s",&a,&b,perm,path)<3) continue;
            if(strchr(perm,'x')==0) continue;
            if(path[0]!='/') continue;
            int dup=0; for(int i=0;i<nl;i++) if(libs[i].lo==a){dup=1;break;}
            if(!dup && nl<64){ snprintf(libs[nl].name,sizeof libs[nl].name,"%s",path); libs[nl].lo=a; libs[nl].hi=b; libs[nl].cnt=0; nl++; }
        }
        if(mf)fclose(mf);
        for(int i=0;i<nsamp;i++){
            unsigned long pc=samples[i];
            for(int j=0;j<nl;j++) if(pc>=libs[j].lo && pc<libs[j].hi){libs[j].cnt++;break;}
        }
        logf("--- PC 分布（按可执行映射）---");
        for(int i=0;i<nl;i++) if(libs[i].cnt>0)
            logf("  %-45s x%d", strrchr(libs[i].name,'/')?strrchr(libs[i].name,'/')+1:libs[i].name, libs[i].cnt);
        /* 保存全部样本以便外部分析 */
        { int f=open("/log/zvegisk_pcraw.bin",O_WRONLY|O_CREAT|O_TRUNC,0644);
          if(f>=0){ write(f,samples,nsamp*sizeof(unsigned long)); close(f); } }
    }

    /* 直方图：按 libObj 偏移统计 */
    struct { unsigned long off; int cnt; } hist[512]; int nh=0;
    int in_libobj=0;
    for (int i=0;i<nsamp;i++) {
        unsigned long pc=samples[i];
        if (pc<lo_base||pc>=lo_end) continue;
        in_libobj++;
        unsigned long off = pc - lo_base;
        int found=-1;
        for (int j=0;j<nh;j++) if(hist[j].off==off){found=j;break;}
        if(found>=0) hist[found].cnt++;
        else if(nh<512){hist[nh].off=off;hist[nh].cnt=1;nh++;}
    }
    logf("in_libobj=%d / %d", in_libobj, nsamp);

    /* 简单排序输出前 25 */
    for (int i=0;i<nh;i++) for(int j=i+1;j<nh;j++)
        if(hist[j].cnt>hist[i].cnt){unsigned long t=hist[i].off;int c=hist[i].cnt;hist[i].off=hist[j].off;hist[i].cnt=hist[j].cnt;hist[j].off=t;hist[j].cnt=c;}
    logf("--- top libObj offsets (off = pc - 0x%lx) ---", lo_base);
    for (int i=0;i<nh && i<25;i++)
        logf("  0x%06lx  x%d", hist[i].off, hist[i].cnt);

    logf("PCSAMPLE_DONE");
    return 0;
}
