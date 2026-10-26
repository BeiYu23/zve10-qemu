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
static void mark(const char *s) { unsigned n=0; while(s[n]) ++n; write(1,s,n); }
static void *load(const char *s) {
    printf("MENUHOST dlopen %s\n",s); fflush(0);
    void *p=dlopen(s,GLOBAL_NOW);
    printf("MENUHOST handle=%p error=%s\n",p,p?"none":dlerror()); fflush(0);
    return p;
}
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
    void *mwf=load("/usr/lib/libMWF.so"); if(!mwf) return 11;
    void (*mwfi)(void)=(void(*)(void))dlsym(mwf,"_ZN3MWF10InitializeEv");
    if(!mwfi) {mark("MENUHOST missing MWF Initialize\n"); return 12;}
    mark("MENUHOST MWF Initialize enter\n"); mwfi(); mark("MENUHOST MWF Initialize returned\n");
    void *obj=load("/usr/lib/libObj.so"); if(!obj) return 13;
    void (*app)(const char *)=(void(*)(const char *))dlsym(obj,"Application_System_Init");
    unsigned *pvm=(unsigned *)dlsym(obj,"pViewManager");
    printf("MENUHOST API=%p pVMslot=%p\n",(void*)app,(void*)pvm); fflush(0);
    if(!app||!pvm) return 14;
    mark("MENUHOST Application_System_Init enter\n"); app(0); mark("MENUHOST Application_System_Init returned\n");
    for(unsigned i=0;i<90;++i) { sleep(1); if(i%5==0) {printf("MENUHOST tick=%u pVM=%08x\n",i,*pvm);fflush(0);} }
    mark("MENUHOST experiment finished\n"); return 0;
}
