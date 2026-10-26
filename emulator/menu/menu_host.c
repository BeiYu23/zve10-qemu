/* Menu-only experiment: run original MWF + libObj entry, not im/IMDB main.
 * Firmware routines, object ownership and drawing remain original.
 * This is an experiment, NOT a menu implementation or success shim.
 */
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern int printf(const char *, ...);
extern int fflush(void *);
extern unsigned sleep(unsigned);
extern long write(int, const void *, unsigned);
#define NEXT ((void *)-1)
#define GLOBAL_NOW 0x102
extern int open(const char *, int, ...);
extern int close(int);
extern void *mmap(void *, unsigned long, int, int, int, unsigned long);
extern void *mmap64(void *, unsigned long, int, int, int, long long);
#define MAP_SHARED_FIXED 0x11
#define PROT_RW 3
#define DDR0_SIZE 0x40000000u
#define DDR1_BASE 0x80000000u
#define DDR1_SIZE 0x10000000u
#define VA_DDR0 0x60000000u
#define VA_DDR1 0xa0000000u
static int phys_map_status;
static int map_phys_ranges(void) {
    int fd=open("/dev/mem",2); if(fd<0) return -1;
    void *a=mmap((void *)(unsigned long)VA_DDR0,DDR0_SIZE,PROT_RW,MAP_SHARED_FIXED,fd,0);
    void *b=mmap64((void *)(unsigned long)VA_DDR1,DDR1_SIZE,PROT_RW,MAP_SHARED_FIXED,fd,0x80000000LL);
    close(fd);
    if(a!=(void *)(unsigned long)VA_DDR0 || b!=(void *)(unsigned long)VA_DDR1) return -1;
    return 0;
}
/* Firmware platform helper: map physical DDR into the Linux ARM user alias.
 * This is an actual shared /dev/mem mapping, not a fabricated address reply. */
unsigned long arch_phys_to_cache(unsigned long pa) {
    if(pa<DDR0_SIZE) {
        if(phys_map_status==0) phys_map_status=map_phys_ranges()? -1:1;
        if(phys_map_status==1) return (unsigned long)pa+VA_DDR0;
    } else if(pa>=DDR1_BASE && pa<DDR1_BASE+DDR1_SIZE) {
        if(phys_map_status==0) phys_map_status=map_phys_ranges()? -1:1;
        if(phys_map_status==1) return (unsigned long)pa-DDR1_BASE+VA_DDR1;
    }
    printf("MENUHOST arch_phys_to_cache rejected pa=%08lx status=%d\\n",pa,phys_map_status); fflush(0);
    return 0;
}
static void mark(const char *s) { unsigned n=0; while(s[n]) ++n; write(1,s,n); }
static void *load_flags(const char *s, int flags) {
    printf("MENUHOST dlopen %s flags=%x\n",s,flags); fflush(0);
    void *p=dlopen(s,flags);
    printf("MENUHOST handle=%p error=%s\n",p,p?"none":dlerror()); fflush(0);
    return p;
}
static void *load(const char *s) { return load_flags(s,GLOBAL_NOW); }
/* The only virtual configuration: documented reserved monitor/display areas.
 * Unknown requests fail explicitly; no invented config tables or fake writes.
 * This executable deliberately replaces the CP Backup transport for these keys.
 */
int osal_snd_sync_msg(unsigned qid, void *msg) {
    static int (*real)(unsigned,void *);
    if(qid==0x003e014d && msg) {
        unsigned *m=msg, value=0, key=m[6]; int known=1;
        switch(key) {
        case 0x00fa0028: value=0x8f000000; break;
        case 0x00fa005d: value=0x00080000; break;
        case 0x00fa00a3: value=0x20312000; break;
        case 0x00fa00a4: value=0x00100000; break;
        default: known=0; break;
        }
        printf("MENUHOST backup count=%u key=%08x supported=%d value=%08x\n",m[2],key,known,value); fflush(0);
        if(!known) return -1024;
        m[1]=value; return 0;
    }
    if(!real) real=(int(*)(unsigned,void *))dlsym(NEXT,"osal_snd_sync_msg");
    return real?real(qid,msg):-1024;
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    mark("MENUHOST main: original im/IMDB main bypassed\n");
    void *uipc=load("/usr/lib/libosal_uipc.so"); if(!uipc) return 10;
    void *utm=load("/usr/lib/libosal_utm.so"); if(!utm) return 10;
    void *backup=load("/usr/lib/libbackup.so"); if(!backup) return 10;
    void *mwf=load("/usr/lib/libMWF.so"); if(!mwf) return 11;
    void (*mwfi)(void)=(void(*)(void))dlsym(mwf,"_ZN3MWF10InitializeEv");
    if(!mwfi) {mark("MENUHOST missing MWF Initialize\n"); return 12;}
    mark("MENUHOST MWF Initialize enter\n"); mwfi(); mark("MENUHOST MWF Initialize returned\n");
    void *obj=load_flags("/usr/lib/libObj.so",0x101); if(!obj) return 13;
    void (*app)(const char *)=(void(*)(const char *))dlsym(obj,"Application_System_Init");
    unsigned *pvm=(unsigned *)dlsym(obj,"pViewManager");
    printf("MENUHOST API=%p pVMslot=%p\n",(void*)app,(void*)pvm); fflush(0);
    if(!app||!pvm) return 14;
    mark("MENUHOST Application_System_Init enter\n"); app(0); mark("MENUHOST Application_System_Init returned\n");
    for(unsigned i=0;i<90;++i) { sleep(1); if((i==0)||(i==5)||(i==10)||(i==15)||(i==20)||(i==25)||(i==30)||(i==35)||(i==40)||(i==45)||(i==50)||(i==55)||(i==60)||(i==65)||(i==70)||(i==75)||(i==80)||(i==85)) {printf("MENUHOST tick=%u pVM=%08x\n",i,*pvm);fflush(0);} }
    mark("MENUHOST experiment finished\n"); return 0;
}
