/* zvegisk_hook2tool.c — 注入带计数器的 setRectColor hook，验证函数是否被调用
 *
 * 计数器地址 = 函数地址 + 0x34（hook 代码最后 4 字节）
 * 用法: zvegisk_hook2tool <pid> [等待秒数]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>

#define OUT   "/log/zvegisk_hook2.log"
#define ORIG  "/log/zvegisk_hook2_orig.bin"
#define SETRECTCOLOR_OFF 0x62c294UL
#define HOOKLEN 56
#define CTR_OFF 0x34

static const unsigned char HOOK[HOOKLEN] = {
    0x0f,0xf2,0x30,0x0c, 0xdc,0xf8,0x00,0x30, 0x01,0x33, 0xcc,0xf8,0x00,0x30,
    0xb0,0xf8,0x8e,0x30, 0x8b,0x42, 0x00,0xd8, 0x70,0x47,
    0xd0,0xf8,0x88,0x30, 0x5f,0xf0,0x2c,0x0c, 0x0c,0xfb,0x01,0x33,
    0x40,0xf2,0xff,0x0c, 0xcf,0xf6,0xff,0x7c, 0xc3,0xf8,0x22,0xc0,
    0x70,0x47, 0xc0,0x46, 0x00,0x00, 0x00,0x00
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

static unsigned int read_ctr(int fd, unsigned long addr){
    unsigned int v=0;
    if(pread(fd,&v,4,(off_t)addr)!=4) return 0xFFFFFFFFu;
    return v;
}

int main(int argc,char**argv)
{
    wrr();
    logf("HOOK2_START uid=%d",geteuid());
    int pid=(argc>1)?atoi(argv[1]):157;
    int wait_s=(argc>2)?atoi(argv[2]):25;
    logf("pid=%d wait=%ds",pid,wait_s);

    int found=0; unsigned long base=find_base(pid,"libObj.so",&found);
    if(!found){logf("NO_LIBOBJ");logf("HOOK2_DONE");return 1;}
    unsigned long addr=base+SETRECTCOLOR_OFF;
    unsigned long ctr =addr+CTR_OFF;
    logf("setRectColor=0x%lx  counter=0x%lx",addr,ctr);

    char mp[64]; snprintf(mp,sizeof mp,"/proc/%d/mem",pid);
    int fd=open(mp,O_RDWR);
    if(fd<0){logf("open FAIL %s",strerror(errno));logf("HOOK2_DONE");return 1;}

    /* 备份 */
    unsigned char orig[HOOKLEN];
    if(pread(fd,orig,HOOKLEN,(off_t)addr)!=HOOKLEN){logf("pread FAIL");close(fd);logf("HOOK2_DONE");return 1;}
    { int f=open(ORIG,O_WRONLY|O_CREAT|O_TRUNC,0644);
      if(f>=0){unsigned char rec[12+HOOKLEN]; memcpy(rec,&addr,8); memcpy(rec+8,orig,HOOKLEN); write(f,rec,sizeof rec); close(f);} }
    logf("备份已存: 原字节头 %02x %02x %02x %02x",orig[0],orig[1],orig[2],orig[3]);

    /* 注入 */
    if(pwrite(fd,HOOK,HOOKLEN,(off_t)addr)!=HOOKLEN){logf("pwrite FAIL %s",strerror(errno));close(fd);logf("HOOK2_DONE");return 1;}
    logf("已注入 56 字节 hook（含计数器）");
    logf("");
    logf("=====================================================");
    logf(">>> 请【打开菜单，并在菜单里上下移动选中项】");
    logf(">>> 持续 %d 秒，我会每 2 秒读一次计数器", wait_s);
    logf("=====================================================");

    /* 轮询计数器 */
    unsigned int last=0;
    for (int t=0; t<wait_s; t+=2) {
        sleep(2);
        unsigned int c=read_ctr(fd,ctr);
        logf("  t=%2ds  计数器 = %u%s", t+2, c, (c!=last)?"  ← 在增长":"");
        last=c;
    }

    logf("");
    logf("=== 最终计数 = %u ===", read_ctr(fd,ctr));
    logf("  > 0  ⇒ setRectColor 被调用（hook 生效）");
    logf("  = 0  ⇒ 这个函数在画界面时根本没被调用");

    /* 恢复 */
    pwrite(fd,orig,HOOKLEN,(off_t)addr);
    unsigned char chk[HOOKLEN];
    pread(fd,chk,HOOKLEN,(off_t)addr);
    logf("恢复验证: %s", memcmp(chk,orig,HOOKLEN)==0?"MATCH ★":"MISMATCH !!");

    close(fd);
    logf("HOOK2_DONE");
    return 0;
}
