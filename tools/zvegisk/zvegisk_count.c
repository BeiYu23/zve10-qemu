/* zvegisk_count.c — 分离式函数调用计数器
 *
 * 用法:
 *   zvegisk_count <pid> <offset> apply    ← 注入，立刻返回
 *   zvegisk_count <pid> <offset> read     ← 读计数器，立刻返回
 *   zvegisk_count <pid> <offset> restore  ← 恢复，立刻返回
 *
 * 注入后你可以慢慢操作相机，最后再 read。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>

#define OUT  "/log/zvegisk_count.log"
#define ORIG "/log/zvegisk_count_orig.bin"
#define HOOKLEN 20
#define CTR_OFF 0x10      /* ★ addw ip,pc,#12 → PC=addr+4 → ip=addr+0x10 */

/* adr ip,ctr ; ldr r3,[ip] ; adds r3,#1 ; str r3,[ip] ; movs r0,#0 ; bx lr */
static const unsigned char HOOK[HOOKLEN] = {
    0x0f,0xf2,0x0c,0x0c,
    0xdc,0xf8,0x00,0x30,
    0x01,0x33,
    0xcc,0xf8,0x00,0x30,
    0x00,0x20,
    0x70,0x47,
    0x00,0x00,
};

static void wrr(void){int fd=open(OUT,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0)close(fd);}
static void wr(const char*s){int fd=open(OUT,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){write(fd,s,strlen(s));close(fd);}}
static void logf(const char*fmt,...){char b[512];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);strncat(b,"\n",sizeof(b)-strlen(b)-1);wr(b);}

static unsigned long find_base(int pid,const char*so,int*f){
    char p[64]; snprintf(p,sizeof p,"/proc/%d/maps",pid);
    FILE*fp=fopen(p,"r"); *f=0; if(!fp)return 0;
    char l[512];
    while(fgets(l,sizeof l,fp)){
        if(!strstr(l,so)||!strstr(l,"r-xp"))continue;
        unsigned long s=0;
        if(sscanf(l,"%lx-%*lx",&s)==1){*f=1;fclose(fp);return s;}
    }
    fclose(fp);return 0;
}

int main(int argc,char**argv)
{
    wrr();
    int pid=(argc>1)?atoi(argv[1]):157;
    unsigned long off=(argc>2)?strtoul(argv[2],0,0):0;
    const char *mode=(argc>3)?argv[3]:"read";
    off &= ~1UL;

    logf("mode=%s pid=%d offset=0x%lx", mode, pid, off);
    int found=0; unsigned long base=find_base(pid,"libObj.so",&found);
    if(!found){logf("NO_LIBOBJ");return 1;}
    unsigned long addr=base+off;
    unsigned long ctr=addr+CTR_OFF;

    char mp[64]; snprintf(mp,sizeof mp,"/proc/%d/mem",pid);
    int fd=open(mp,O_RDWR);
    if(fd<0){logf("open FAIL %s",strerror(errno));return 1;}

    if(!strcmp(mode,"apply")){
        unsigned char orig[HOOKLEN];
        if(pread(fd,orig,HOOKLEN,(off_t)addr)!=HOOKLEN){logf("pread FAIL");close(fd);return 1;}
        int f=open(ORIG,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(f>=0){unsigned char rec[8+HOOKLEN]; memcpy(rec,&addr,8); memcpy(rec+8,orig,HOOKLEN); write(f,rec,sizeof rec); close(f);}
        if(pwrite(fd,HOOK,HOOKLEN,(off_t)addr)!=HOOKLEN){logf("pwrite FAIL %s",strerror(errno));close(fd);return 1;}
        logf("APPLIED target=0x%lx ctr=0x%lx",addr,ctr);
        logf("原字节: %02x %02x %02x %02x",orig[0],orig[1],orig[2],orig[3]);
    } else if(!strcmp(mode,"read")){
        unsigned int c=0xFFFFFFFFu;
        pread(fd,&c,4,(off_t)ctr);
        logf("COUNTER=%u (target=0x%lx ctr=0x%lx)",c,addr,ctr);
    } else if(!strcmp(mode,"restore")){
        unsigned char rec[8+HOOKLEN];
        int f=open(ORIG,O_RDONLY);
        if(f<0){logf("no orig file");close(fd);return 1;}
        int n=read(f,rec,sizeof rec); close(f);
        if(n!=(int)sizeof rec){logf("short read");close(fd);return 1;}
        unsigned long a=0; memcpy(&a,rec,8);
        pwrite(fd,rec+8,HOOKLEN,(off_t)a);
        unsigned char chk[HOOKLEN]; pread(fd,chk,HOOKLEN,(off_t)a);
        logf("RESTORED 0x%lx verify=%s", a, memcmp(chk,rec+8,HOOKLEN)==0?"MATCH ★":"MISMATCH !!");
    }
    close(fd);
    return 0;
}
