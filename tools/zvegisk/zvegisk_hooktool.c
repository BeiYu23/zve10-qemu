/* zvegisk_hooktool.c — 把 setRectColor 替换成"总是写品红色"
 *
 * 目标函数：ux::gfxeng::Sprite::setRectColor(u16, CColor const&)  @ libObj+0x62c294
 * 函数长度 80 字节，我们的替代代码 36 字节 —— 放得下。
 *
 * 替代逻辑：
 *   r0 = Sprite*, r1 = rectIdx, r2 = &CColor
 *   检查 idx 范围 → 计算 &rect[idx] → 写入品红 (0xFFFF00FF) → 返回
 *
 * 用法：
 *   zvegisk_hooktool <pid> save     备份原始 36 字节
 *   zvegisk_hooktool <pid> apply    注入
 *   zvegisk_hooktool <pid> restore  恢复
 *   zvegisk_hooktool <pid> test     备份 → 注入 → 等 20s → 恢复
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>

#define OUT    "/log/zvegisk_hook.log"
#define ORIG   "/log/zvegisk_hook_orig.bin"
#define SETRECTCOLOR_OFF 0x62c294UL
#define HOOKLEN 36

static const unsigned char HOOK[HOOKLEN] = {
    0xb0, 0xf8, 0x8e, 0x30, 0x8b, 0x42, 0x00, 0xd8, 0x70, 0x47, 0xd0, 0xf8,
    0x88, 0x30, 0x5f, 0xf0, 0x2c, 0x0c, 0x0c, 0xfb, 0x01, 0x33, 0x40, 0xf2,
    0xff, 0x0c, 0xcf, 0xf6, 0xff, 0x7c, 0xc3, 0xf8, 0x22, 0xc0, 0x70, 0x47,
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

static int do_save(int fd, unsigned long addr){
    unsigned char b[HOOKLEN];
    if(pread(fd,b,HOOKLEN,(off_t)addr)!=HOOKLEN){logf("save FAIL %s",strerror(errno));return -1;}
    int f=open(ORIG,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(f<0){logf("save open FAIL %s",strerror(errno));return -1;}
    open(ORIG,O_WRONLY|O_CREAT|O_TRUNC,0644);
    unsigned char rec[12+HOOKLEN];
    memcpy(rec,&addr,8); memcpy(rec+8,b,HOOKLEN);
    int w=write(f,rec,sizeof rec); close(f);
    logf("save: addr=0x%lx %d bytes (wrote %d)",addr,HOOKLEN,w);
    logf("  原字节头 8: %02x %02x %02x %02x %02x %02x %02x %02x",
         b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7]);
    return 0;
}

static int do_apply(int fd, unsigned long addr){
    ssize_t w=pwrite(fd,HOOK,HOOKLEN,(off_t)addr);
    logf("apply: pwrite=%zd %s",w,w<0?strerror(errno):"OK");
    if(w!=HOOKLEN)return -1;
    unsigned char chk[HOOKLEN];
    pread(fd,chk,HOOKLEN,(off_t)addr);
    logf("apply verify: %02x %02x %02x %02x ... (期望 b0 f8 8e 30)",chk[0],chk[1],chk[2],chk[3]);
    return memcmp(chk,HOOK,HOOKLEN)==0?0:-1;
}

static int do_restore(int fd){
    unsigned char rec[12+HOOKLEN];
    int f=open(ORIG,O_RDONLY);
    if(f<0){logf("restore: no %s",ORIG);return -1;}
    int n=read(f,rec,sizeof rec); close(f);
    if(n!=(int)sizeof rec){logf("restore: short %d",n);return -1;}
    unsigned long addr=0; memcpy(&addr,rec,8);
    ssize_t w=pwrite(fd,rec+8,HOOKLEN,(off_t)addr);
    logf("restore: addr=0x%lx pwrite=%zd %s",addr,w,w<0?strerror(errno):"OK");
    unsigned char chk[HOOKLEN];
    pread(fd,chk,HOOKLEN,(off_t)addr);
    logf("restore verify: %s", memcmp(chk,rec+8,HOOKLEN)==0?"MATCH ★":"MISMATCH !!");
    return 0;
}

int main(int argc,char**argv)
{
    wrr();
    logf("HOOKTOOL_START uid=%d",geteuid());
    int pid=(argc>1)?atoi(argv[1]):157;
    const char*mode=(argc>2)?argv[2]:"test";
    logf("pid=%d mode=%s",pid,mode);

    int found=0; unsigned long base=find_base(pid,"libObj.so",&found);
    if(!found){logf("NO_LIBOBJ");logf("HOOKTOOL_DONE");return 1;}
    unsigned long addr=base+SETRECTCOLOR_OFF;
    logf("libObj base=0x%lx  target=0x%lx (setRectColor)",base,addr);

    char mp[64]; snprintf(mp,sizeof mp,"/proc/%d/mem",pid);
    int fd=open(mp,O_RDWR);
    logf("open %s = %d %s",mp,fd,fd<0?strerror(errno):"");
    if(fd<0){logf("HOOKTOOL_DONE");return 1;}

    if(!strcmp(mode,"save")) do_save(fd,addr);
    else if(!strcmp(mode,"apply")){ if(do_save(fd,addr)==0) do_apply(fd,addr); }
    else if(!strcmp(mode,"restore")) do_restore(fd);
    else {
        if(do_save(fd,addr)==0 && do_apply(fd,addr)==0){
            logf("--- 已注入，保持 20 秒：请翻菜单/按按键，UI 矩形应变成品红 ---");
            sync(); sleep(20);
            logf("--- 恢复 ---");
            do_restore(fd);
        }
    }
    close(fd);
    logf("HOOKTOOL_DONE");
    return 0;
}
