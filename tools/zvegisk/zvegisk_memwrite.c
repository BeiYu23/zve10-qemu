/* zvegisk_memwrite.c — 验证能否【写】im.elf 的代码
 *
 * ★ 安全策略：读出原始字节 → 原样写回 → 验证一致
 *   ⇒ 数据一个字节都没变，但证明了 pwrite/ptrace 写权限存在
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>

#define OUT "/log/zvegisk_mw.log"
#define FILLRECT_VADDR 0x683f4cUL

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static unsigned long find_base(int pid,const char*so,int*f){
    char p[64]; snprintf(p,sizeof p,"/proc/%d/maps",pid);
    FILE*fp=fopen(p,"r"); *f=0; if(!fp)return 0;
    char l[512];
    while(fgets(l,sizeof l,fp)){
        if(!strstr(l,so)||!strstr(l,"r-xp"))continue;
        unsigned long s=0; unsigned long off=0;
        if(sscanf(l,"%lx-%*lx %*s %lx",&s,&off)>=1){*f=1;fclose(fp);return s;}
    }
    fclose(fp);return 0;
}

int main(int argc,char**argv)
{
    wrr();
    logf("MEMWRITE_START uid=%d",geteuid());
    int pid=(argc>1)?atoi(argv[1]):157;

    int found=0; unsigned long base=find_base(pid,"libObj.so",&found);
    logf("libObj base=0x%lx found=%d",base,found);
    if(!found){logf("NO_BASE");logf("MEMWRITE_DONE");return 1;}

    unsigned long target = base + FILLRECT_VADDR;
    logf("target GRM_gpermFillrect @0x%lx",target);

    /* ---- 方法 A：直接 pwrite /proc/PID/mem ---- */
    char mp[64]; snprintf(mp,sizeof mp,"/proc/%d/mem",pid);
    int fd=open(mp,O_RDWR);
    logf("open %s O_RDWR = %d%s",mp,fd,fd<0?strerror(errno):"");
    if(fd>=0){
        unsigned char orig[8];
        int n=pread(fd,orig,8,(off_t)target);
        logf("pread 8 = %d bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
             n,orig[0],orig[1],orig[2],orig[3],orig[4],orig[5],orig[6],orig[7]);
        if(n==8){
            ssize_t w = pwrite(fd,orig,8,(off_t)target);   /* ★ 写回相同字节 */
            logf("pwrite(same bytes) = %zd%s", w, w<0?strerror(errno):"");
            unsigned char chk[8];
            int n2=pread(fd,chk,8,(off_t)target);
            logf("re-read = %d, identical=%s",
                 n2, (n2==8 && memcmp(orig,chk,8)==0)?"YES ★":"no");
        }
        close(fd);
    }

    /* ---- 方法 B：ptrace POKETEXT ---- */
    logf("--- ptrace 方式 ---");
    if(ptrace(PTRACE_ATTACH,pid,0,0)<0){
        logf("PTRACE_ATTACH failed: %s",strerror(errno));
    } else {
        int st=0; waitpid(pid,&st,0);
        logf("attached, wait status=0x%x",st);

        errno=0;
        long w1 = ptrace(PTRACE_PEEKTEXT,pid,(void*)target,0);
        logf("PEEKTEXT @0x%lx = 0x%08lx (errno=%d)",target,w1,errno);

        if(errno==0){
            long res = ptrace(PTRACE_POKETEXT,pid,(void*)target,(void*)w1);  /* 写回相同值 */
            logf("POKETEXT(same value) = %ld (errno=%d %s)",res,errno,res<0?strerror(errno):"OK");
            if(res>=0){
                long w2 = ptrace(PTRACE_PEEKTEXT,pid,(void*)target,0);
                logf("re-PEEK = 0x%08lx identical=%s",w2,(w2==w1)?"YES ★":"no");
            }
        }
        ptrace(PTRACE_DETACH,pid,0,0);
        logf("detached");
    }

    logf("MEMWRITE_DONE (nothing actually modified)");
    return 0;
}
