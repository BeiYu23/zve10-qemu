/* libgrm.so —— Phase 1 录制桩 */
#define NULL ((void*)0)
static long sys_write(int fd, const void *buf, unsigned long n)
{
    register long r7 __asm__("r7") = 4;
    register long r0 __asm__("r0") = fd;
    register long r1 __asm__("r1") = (long)buf;
    register long r2 __asm__("r2") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static long sys_open(const char *p, int fl, int mode)
{
    register long r7 __asm__("r7") = 5;
    register long r0 __asm__("r0") = (long)p;
    register long r1 __asm__("r1") = fl;
    register long r2 __asm__("r2") = mode;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static char tbuf[256];
static int  tlen;
static void tc(char c){ if (tlen < (int)sizeof(tbuf)-1) tbuf[tlen++] = c; }
static void ts(const char *s){ while (*s) tc(*s++); }
static void thex(unsigned long v){ int i; for (i=28;i>=0;i-=4) tc("0123456789abcdef"[(v>>i)&0xf]); }
static void tflush(void){ if (tlen) { sys_write(1, tbuf, tlen); tlen = 0; } }
#define LOG_MAX 4000
static int log_budget = LOG_MAX;
static void rec(const char *name, unsigned int a1, unsigned int a2, unsigned int a3, unsigned int a4)
{
    if (log_budget <= 0) return;
    log_budget--;
    tlen = 0;
    ts("[grm] "); ts(name);
    ts("("); thex(a1); ts(","); thex(a2); ts(","); thex(a3); ts(","); thex(a4); ts(")\n");
    tflush();
}
#define FB_W 640
#define FB_H 480
static unsigned char fb[FB_W*FB_H*4];
static void dump_fb(void)
{
    int fd = (int)sys_open("/tmp/fb.raw", 0x241, 0644);
    if (fd < 0) { tlen=0; ts("[grm] !! cannot open /tmp/fb.raw\n"); tflush(); return; }
    sys_write(fd, &fb[0], sizeof(fb));
    tlen=0; ts("[grm] >>> dumped framebuffer 640x480 -> /tmp/fb.raw\n"); tflush();
}
static const char *S_GL_VERSION = "OpenGL ES 2.0";
static const char *S_GL_VENDOR  = "DMP";
static const char *S_GL_RENDERER= "SMAPH-S";
static const char *S_GL_EXT     = "GL_OES_EGL_image GL_OES_depth24";
static const char *S_EGL_VER    = "1.4";
static const char *S_EGL_VENDOR = "DMP";
static const char *S_EGL_EXT    = "EGL_KHR_image_base EGL_KHR_fence_sync";
static unsigned int shader_ctr, program_ctr, loc_ctr, attr_ctr, buf_ctr, tex_ctr, fbo_ctr, rbo_ctr;
unsigned int eglAsyncSwapBuffersDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglAsyncSwapBuffersDMP",a1,a2,a3,a4); dump_fb(); return 1; }
unsigned int eglBindAPI(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglBindAPI",a1,a2,a3,a4); return 1; }
unsigned int eglBindTexImage(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglBindTexImage",a1,a2,a3,a4); return 0; }
unsigned int eglChooseConfig(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglChooseConfig",a1,a2,a3,a4); if(a4)((unsigned int*)a4)[0]=1; if(a3)((unsigned int*)a3)[0]=0x2000; return 1; }
unsigned int eglClientWaitSyncKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglClientWaitSyncKHR",a1,a2,a3,a4); return 0x30F5; }
unsigned int eglCopyBuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCopyBuffers",a1,a2,a3,a4); return 0; }
unsigned int eglCreateContext(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreateContext",a1,a2,a3,a4); return 0x4000; }
unsigned int eglCreateImageKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreateImageKHR",a1,a2,a3,a4); return 0; }
unsigned int eglCreatePbufferFromClientBuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreatePbufferFromClientBuffer",a1,a2,a3,a4); return 0x3003; }
unsigned int eglCreatePbufferSurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreatePbufferSurface",a1,a2,a3,a4); return 0x3001; }
unsigned int eglCreatePixmapSurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreatePixmapSurface",a1,a2,a3,a4); return 0x3002; }
unsigned int eglCreateSyncKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreateSyncKHR",a1,a2,a3,a4); return 0x5000; }
unsigned int eglCreateWindowSurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglCreateWindowSurface",a1,a2,a3,a4); return 0x3000; }
unsigned int eglDestroyContext(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglDestroyContext",a1,a2,a3,a4); return 1; }
unsigned int eglDestroyImageKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglDestroyImageKHR",a1,a2,a3,a4); return 0; }
unsigned int eglDestroySurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglDestroySurface",a1,a2,a3,a4); return 1; }
unsigned int eglDestroySyncKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglDestroySyncKHR",a1,a2,a3,a4); return 1; }
unsigned int eglDrawFrameModeDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglDrawFrameModeDMP",a1,a2,a3,a4); return 0; }
unsigned int eglGetConfigAttrib(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetConfigAttrib",a1,a2,a3,a4); if(a4)((unsigned int*)a4)[0]=(a3==0x3025?32:0x4000); return 1; }
unsigned int eglGetConfigs(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetConfigs",a1,a2,a3,a4); if(a4)((unsigned int*)a4)[0]=1; if(a3)((unsigned int*)a3)[0]=0x2000; return 1; }
unsigned int eglGetCurrentContext(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetCurrentContext",a1,a2,a3,a4); return 0x4000; }
unsigned int eglGetCurrentDisplay(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetCurrentDisplay",a1,a2,a3,a4); return 0x1000; }
unsigned int eglGetCurrentSurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetCurrentSurface",a1,a2,a3,a4); return 0x3000; }
unsigned int eglGetDisplay(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetDisplay",a1,a2,a3,a4); return 0x1000; }
unsigned int eglGetError(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetError",a1,a2,a3,a4); return 0x3000; }
unsigned int eglGetSyncAttribKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglGetSyncAttribKHR",a1,a2,a3,a4); if(a3)((unsigned int*)a3)[0]=1; return 1; }
unsigned int eglInitialize(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglInitialize",a1,a2,a3,a4); ((unsigned int*)a2)[0]=1; ((unsigned int*)a3)[0]=4; return 1; }
unsigned int eglInvalidateImageDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglInvalidateImageDMP",a1,a2,a3,a4); return 1; }
unsigned int eglMakeCurrent(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglMakeCurrent",a1,a2,a3,a4); return 1; }
unsigned int eglQueryAPI(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQueryAPI",a1,a2,a3,a4); return 0; }
unsigned int eglQueryContext(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQueryContext",a1,a2,a3,a4); return 0; }
unsigned int eglQueryDisplayDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQueryDisplayDMP",a1,a2,a3,a4); return 0x1000; }
unsigned int eglQueryFrameDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQueryFrameDMP",a1,a2,a3,a4); return 0; }
unsigned int eglQueryString(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQueryString",a1,a2,a3,a4); switch(a2){case 0x3053:return (unsigned int)S_EGL_VENDOR; case 0x3054:return (unsigned int)S_EGL_VER; case 0x3055:return (unsigned int)S_EGL_EXT;} return (unsigned int)S_EGL_VER; }
unsigned int eglQuerySurface(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQuerySurface",a1,a2,a3,a4); if(a4)((unsigned int*)a4)[0]=(a3==0x3056?FB_W:(a3==0x3057?FB_H:0x0008)); return 1; }
unsigned int eglQuerySurfacePointerANGLE(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglQuerySurfacePointerANGLE",a1,a2,a3,a4); return 0; }
unsigned int eglReleaseTexImage(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglReleaseTexImage",a1,a2,a3,a4); return 0; }
unsigned int eglReleaseThread(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglReleaseThread",a1,a2,a3,a4); return 1; }
unsigned int eglResumeDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglResumeDMP",a1,a2,a3,a4); return 1; }
unsigned int eglSetHardwareStateDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSetHardwareStateDMP",a1,a2,a3,a4); return 1; }
unsigned int eglSignalSyncKHR(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSignalSyncKHR",a1,a2,a3,a4); return 1; }
unsigned int eglSurfaceAttrib(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSurfaceAttrib",a1,a2,a3,a4); return 1; }
unsigned int eglSuspendDMP(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSuspendDMP",a1,a2,a3,a4); return 1; }
unsigned int eglSwapBuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSwapBuffers",a1,a2,a3,a4); dump_fb(); return 1; }
unsigned int eglSwapInterval(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglSwapInterval",a1,a2,a3,a4); return 1; }
unsigned int eglTerminate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglTerminate",a1,a2,a3,a4); return 1; }
unsigned int eglWaitClient(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglWaitClient",a1,a2,a3,a4); return 1; }
unsigned int eglWaitGL(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglWaitGL",a1,a2,a3,a4); return 1; }
unsigned int eglWaitNative(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("eglWaitNative",a1,a2,a3,a4); return 1; }
unsigned int glActiveTexture(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glActiveTexture",a1,a2,a3,a4); return 0; }
unsigned int glAttachShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glAttachShader",a1,a2,a3,a4); return 0; }
unsigned int glBindAttribLocation(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBindAttribLocation",a1,a2,a3,a4); return 0; }
unsigned int glBindBuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBindBuffer",a1,a2,a3,a4); return 0; }
unsigned int glBindFramebuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBindFramebuffer",a1,a2,a3,a4); return 0; }
unsigned int glBindRenderbuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBindRenderbuffer",a1,a2,a3,a4); return 0; }
unsigned int glBindTexture(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBindTexture",a1,a2,a3,a4); return 0; }
unsigned int glBlendColor(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBlendColor",a1,a2,a3,a4); return 0; }
unsigned int glBlendEquation(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBlendEquation",a1,a2,a3,a4); return 0; }
unsigned int glBlendEquationSeparate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBlendEquationSeparate",a1,a2,a3,a4); return 0; }
unsigned int glBlendFunc(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBlendFunc",a1,a2,a3,a4); return 0; }
unsigned int glBlendFuncSeparate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBlendFuncSeparate",a1,a2,a3,a4); return 0; }
unsigned int glBufferData(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBufferData",a1,a2,a3,a4); return 0; }
unsigned int glBufferSubData(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glBufferSubData",a1,a2,a3,a4); return 0; }
unsigned int glCheckFramebufferStatus(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCheckFramebufferStatus",a1,a2,a3,a4); return 0x8CD5; }
unsigned int glClear(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glClear",a1,a2,a3,a4); return 0; }
unsigned int glClearColor(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glClearColor",a1,a2,a3,a4); return 0; }
unsigned int glClearDepthf(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glClearDepthf",a1,a2,a3,a4); return 0; }
unsigned int glClearStencil(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glClearStencil",a1,a2,a3,a4); return 0; }
unsigned int glColorMask(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glColorMask",a1,a2,a3,a4); return 0; }
unsigned int glCompileShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCompileShader",a1,a2,a3,a4); return 0; }
unsigned int glCompressedTexImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCompressedTexImage2D",a1,a2,a3,a4); return 0; }
unsigned int glCompressedTexSubImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCompressedTexSubImage2D",a1,a2,a3,a4); return 0; }
unsigned int glCopyTexImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCopyTexImage2D",a1,a2,a3,a4); return 0; }
unsigned int glCopyTexSubImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCopyTexSubImage2D",a1,a2,a3,a4); return 0; }
unsigned int glCreateProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCreateProgram",a1,a2,a3,a4); return ++program_ctr; }
unsigned int glCreateShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCreateShader",a1,a2,a3,a4); return ++shader_ctr; }
unsigned int glCullFace(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glCullFace",a1,a2,a3,a4); return 0; }
unsigned int glDeleteBuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteBuffers",a1,a2,a3,a4); return 0; }
unsigned int glDeleteFramebuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteFramebuffers",a1,a2,a3,a4); return 0; }
unsigned int glDeleteProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteProgram",a1,a2,a3,a4); return 0; }
unsigned int glDeleteRenderbuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteRenderbuffers",a1,a2,a3,a4); return 0; }
unsigned int glDeleteShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteShader",a1,a2,a3,a4); return 0; }
unsigned int glDeleteTextures(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDeleteTextures",a1,a2,a3,a4); return 0; }
unsigned int glDepthFunc(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDepthFunc",a1,a2,a3,a4); return 0; }
unsigned int glDepthMask(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDepthMask",a1,a2,a3,a4); return 0; }
unsigned int glDepthRangef(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDepthRangef",a1,a2,a3,a4); return 0; }
unsigned int glDetachShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDetachShader",a1,a2,a3,a4); return 0; }
unsigned int glDisable(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDisable",a1,a2,a3,a4); return 0; }
unsigned int glDisableVertexAttribArray(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDisableVertexAttribArray",a1,a2,a3,a4); return 0; }
unsigned int glDrawArrays(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDrawArrays",a1,a2,a3,a4); return 0; }
unsigned int glDrawElements(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glDrawElements",a1,a2,a3,a4); return 0; }
unsigned int glEGLImageTargetRenderbufferStorageOES(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glEGLImageTargetRenderbufferStorageOES",a1,a2,a3,a4); return 0; }
unsigned int glEGLImageTargetTexture2DOES(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glEGLImageTargetTexture2DOES",a1,a2,a3,a4); return 0; }
unsigned int glEnable(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glEnable",a1,a2,a3,a4); return 0; }
unsigned int glEnableVertexAttribArray(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glEnableVertexAttribArray",a1,a2,a3,a4); return 0; }
unsigned int glFinish(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glFinish",a1,a2,a3,a4); return 0; }
unsigned int glFlush(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glFlush",a1,a2,a3,a4); return 0; }
unsigned int glFramebufferRenderbuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glFramebufferRenderbuffer",a1,a2,a3,a4); return 0; }
unsigned int glFramebufferTexture2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glFramebufferTexture2D",a1,a2,a3,a4); return 0; }
unsigned int glFrontFace(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glFrontFace",a1,a2,a3,a4); return 0; }
unsigned int glGenBuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGenBuffers",a1,a2,a3,a4); if(a2){unsigned int i;for(i=0;i<a1;i++)((unsigned int*)a2)[i]=++buf_ctr;} return 0; }
unsigned int glGenFramebuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGenFramebuffers",a1,a2,a3,a4); if(a2){unsigned int i;for(i=0;i<a1;i++)((unsigned int*)a2)[i]=++fbo_ctr;} return 0; }
unsigned int glGenRenderbuffers(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGenRenderbuffers",a1,a2,a3,a4); if(a2){unsigned int i;for(i=0;i<a1;i++)((unsigned int*)a2)[i]=++rbo_ctr;} return 0; }
unsigned int glGenTextures(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGenTextures",a1,a2,a3,a4); if(a2){unsigned int i;for(i=0;i<a1;i++)((unsigned int*)a2)[i]=++tex_ctr;} return 0; }
unsigned int glGenerateMipmap(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGenerateMipmap",a1,a2,a3,a4); return 0; }
unsigned int glGetActiveAttrib(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetActiveAttrib",a1,a2,a3,a4); return 0; }
unsigned int glGetActiveUniform(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetActiveUniform",a1,a2,a3,a4); return 0; }
unsigned int glGetAttachedShaders(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetAttachedShaders",a1,a2,a3,a4); return 0; }
unsigned int glGetAttribLocation(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetAttribLocation",a1,a2,a3,a4); return ++attr_ctr; }
unsigned int glGetBooleanv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetBooleanv",a1,a2,a3,a4); if(a2)((unsigned int*)a2)[0]=0; return 0; }
unsigned int glGetBufferParameteriv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetBufferParameteriv",a1,a2,a3,a4); return 0; }
unsigned int glGetError(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetError",a1,a2,a3,a4); return 0; }
unsigned int glGetFloatv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetFloatv",a1,a2,a3,a4); if(a2){((unsigned int*)a2)[0]=1;((unsigned int*)a2)[1]=1;((unsigned int*)a2)[2]=0;((unsigned int*)a2)[3]=0;} return 0; }
unsigned int glGetFramebufferAttachmentParameteriv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetFramebufferAttachmentParameteriv",a1,a2,a3,a4); return 0; }
unsigned int glGetIntegerv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetIntegerv",a1,a2,a3,a4); {unsigned int *p=(unsigned int*)a2; if(p){switch(a1){case 0x0D3A:case 0x0D33:p[0]=4096;break; case 0x0BA2:p[0]=0;p[1]=0;p[2]=FB_W;p[3]=FB_H;break; default:p[0]=0;break;}} return 0;} }
unsigned int glGetProgramBinaryOES(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetProgramBinaryOES",a1,a2,a3,a4); return 0; }
unsigned int glGetProgramInfoLog(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetProgramInfoLog",a1,a2,a3,a4); if(a2)((unsigned int*)a2)[0]=0; return 0; }
unsigned int glGetProgramiv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetProgramiv",a1,a2,a3,a4); if(a3)((unsigned int*)a3)[0]=(a2==0x8B82||a2==0x8B80)?1:0; return 0; }
unsigned int glGetRenderbufferParameteriv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetRenderbufferParameteriv",a1,a2,a3,a4); return 0; }
unsigned int glGetShaderInfoLog(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetShaderInfoLog",a1,a2,a3,a4); if(a2)((unsigned int*)a2)[0]=0; return 0; }
unsigned int glGetShaderPrecisionFormat(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetShaderPrecisionFormat",a1,a2,a3,a4); if(a3){((unsigned int*)a3)[0]=23;((unsigned int*)a3)[1]=0x7f7fffff;} if(a4)((unsigned int*)a4)[0]=0; return 0; }
unsigned int glGetShaderSource(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetShaderSource",a1,a2,a3,a4); return 0; }
unsigned int glGetShaderiv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetShaderiv",a1,a2,a3,a4); if(a3)((unsigned int*)a3)[0]=(a2==0x8B81||a2==0x8B80)?1:0; return 0; }
unsigned int glGetString(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetString",a1,a2,a3,a4); switch(a1){case 0x1F02:return (unsigned int)S_GL_VERSION; case 0x1F00:return (unsigned int)S_GL_VENDOR; case 0x1F01:return (unsigned int)S_GL_RENDERER; case 0x1F03:return (unsigned int)S_GL_EXT;} return (unsigned int)S_GL_VERSION; }
unsigned int glGetTexParameterfv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetTexParameterfv",a1,a2,a3,a4); return 0; }
unsigned int glGetTexParameteriv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetTexParameteriv",a1,a2,a3,a4); return 0; }
unsigned int glGetUniformLocation(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetUniformLocation",a1,a2,a3,a4); return ++loc_ctr; }
unsigned int glGetUniformfv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetUniformfv",a1,a2,a3,a4); return 0; }
unsigned int glGetUniformiv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetUniformiv",a1,a2,a3,a4); return 0; }
unsigned int glGetVertexAttribPointerv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetVertexAttribPointerv",a1,a2,a3,a4); return 0; }
unsigned int glGetVertexAttribfv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetVertexAttribfv",a1,a2,a3,a4); return 0; }
unsigned int glGetVertexAttribiv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glGetVertexAttribiv",a1,a2,a3,a4); return 0; }
unsigned int glHint(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glHint",a1,a2,a3,a4); return 0; }
unsigned int glIsBuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsBuffer",a1,a2,a3,a4); return 1; }
unsigned int glIsEnabled(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsEnabled",a1,a2,a3,a4); return 0; }
unsigned int glIsFramebuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsFramebuffer",a1,a2,a3,a4); return 0; }
unsigned int glIsProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsProgram",a1,a2,a3,a4); return 0; }
unsigned int glIsRenderbuffer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsRenderbuffer",a1,a2,a3,a4); return 0; }
unsigned int glIsShader(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsShader",a1,a2,a3,a4); return 0; }
unsigned int glIsTexture(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glIsTexture",a1,a2,a3,a4); return 1; }
unsigned int glLineWidth(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glLineWidth",a1,a2,a3,a4); return 0; }
unsigned int glLinkProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glLinkProgram",a1,a2,a3,a4); return 0; }
unsigned int glPixelStorei(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glPixelStorei",a1,a2,a3,a4); return 0; }
unsigned int glPolygonOffset(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glPolygonOffset",a1,a2,a3,a4); return 0; }
unsigned int glProgramBinaryOES(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glProgramBinaryOES",a1,a2,a3,a4); return 0; }
unsigned int glReadPixels(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glReadPixels",a1,a2,a3,a4); return 0; }
unsigned int glReleaseShaderCompiler(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glReleaseShaderCompiler",a1,a2,a3,a4); return 0; }
unsigned int glRenderbufferStorage(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glRenderbufferStorage",a1,a2,a3,a4); return 0; }
unsigned int glSampleCoverage(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glSampleCoverage",a1,a2,a3,a4); return 0; }
unsigned int glScissor(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glScissor",a1,a2,a3,a4); return 0; }
unsigned int glShaderBinary(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glShaderBinary",a1,a2,a3,a4); return 0; }
unsigned int glShaderSource(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glShaderSource",a1,a2,a3,a4); return 0; }
unsigned int glStencilFunc(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilFunc",a1,a2,a3,a4); return 0; }
unsigned int glStencilFuncSeparate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilFuncSeparate",a1,a2,a3,a4); return 0; }
unsigned int glStencilMask(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilMask",a1,a2,a3,a4); return 0; }
unsigned int glStencilMaskSeparate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilMaskSeparate",a1,a2,a3,a4); return 0; }
unsigned int glStencilOp(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilOp",a1,a2,a3,a4); return 0; }
unsigned int glStencilOpSeparate(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glStencilOpSeparate",a1,a2,a3,a4); return 0; }
unsigned int glTexImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexImage2D",a1,a2,a3,a4); return 0; }
unsigned int glTexParameterf(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexParameterf",a1,a2,a3,a4); return 0; }
unsigned int glTexParameterfv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexParameterfv",a1,a2,a3,a4); return 0; }
unsigned int glTexParameteri(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexParameteri",a1,a2,a3,a4); return 0; }
unsigned int glTexParameteriv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexParameteriv",a1,a2,a3,a4); return 0; }
unsigned int glTexSubImage2D(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glTexSubImage2D",a1,a2,a3,a4); return 0; }
unsigned int glUniform1f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform1f",a1,a2,a3,a4); return 0; }
unsigned int glUniform1fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform1fv",a1,a2,a3,a4); return 0; }
unsigned int glUniform1i(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform1i",a1,a2,a3,a4); return 0; }
unsigned int glUniform1iv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform1iv",a1,a2,a3,a4); return 0; }
unsigned int glUniform2f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform2f",a1,a2,a3,a4); return 0; }
unsigned int glUniform2fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform2fv",a1,a2,a3,a4); return 0; }
unsigned int glUniform2i(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform2i",a1,a2,a3,a4); return 0; }
unsigned int glUniform2iv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform2iv",a1,a2,a3,a4); return 0; }
unsigned int glUniform3f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform3f",a1,a2,a3,a4); return 0; }
unsigned int glUniform3fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform3fv",a1,a2,a3,a4); return 0; }
unsigned int glUniform3i(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform3i",a1,a2,a3,a4); return 0; }
unsigned int glUniform3iv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform3iv",a1,a2,a3,a4); return 0; }
unsigned int glUniform4f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform4f",a1,a2,a3,a4); return 0; }
unsigned int glUniform4fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform4fv",a1,a2,a3,a4); return 0; }
unsigned int glUniform4i(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform4i",a1,a2,a3,a4); return 0; }
unsigned int glUniform4iv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniform4iv",a1,a2,a3,a4); return 0; }
unsigned int glUniformMatrix2fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniformMatrix2fv",a1,a2,a3,a4); return 0; }
unsigned int glUniformMatrix3fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniformMatrix3fv",a1,a2,a3,a4); return 0; }
unsigned int glUniformMatrix4fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUniformMatrix4fv",a1,a2,a3,a4); return 0; }
unsigned int glUseProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glUseProgram",a1,a2,a3,a4); return 0; }
unsigned int glValidateProgram(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glValidateProgram",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib1f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib1f",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib1fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib1fv",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib2f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib2f",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib2fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib2fv",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib3f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib3f",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib3fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib3fv",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib4f(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib4f",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttrib4fv(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttrib4fv",a1,a2,a3,a4); return 0; }
unsigned int glVertexAttribPointer(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glVertexAttribPointer",a1,a2,a3,a4); return 0; }
unsigned int glViewport(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4){ rec("glViewport",a1,a2,a3,a4); return 0; }

struct ent { const char *n; unsigned int f; };
static const struct ent g_tab[] = {
  { "eglAsyncSwapBuffersDMP", (unsigned int)&eglAsyncSwapBuffersDMP },
  { "eglBindAPI", (unsigned int)&eglBindAPI },
  { "eglBindTexImage", (unsigned int)&eglBindTexImage },
  { "eglChooseConfig", (unsigned int)&eglChooseConfig },
  { "eglClientWaitSyncKHR", (unsigned int)&eglClientWaitSyncKHR },
  { "eglCopyBuffers", (unsigned int)&eglCopyBuffers },
  { "eglCreateContext", (unsigned int)&eglCreateContext },
  { "eglCreateImageKHR", (unsigned int)&eglCreateImageKHR },
  { "eglCreatePbufferFromClientBuffer", (unsigned int)&eglCreatePbufferFromClientBuffer },
  { "eglCreatePbufferSurface", (unsigned int)&eglCreatePbufferSurface },
  { "eglCreatePixmapSurface", (unsigned int)&eglCreatePixmapSurface },
  { "eglCreateSyncKHR", (unsigned int)&eglCreateSyncKHR },
  { "eglCreateWindowSurface", (unsigned int)&eglCreateWindowSurface },
  { "eglDestroyContext", (unsigned int)&eglDestroyContext },
  { "eglDestroyImageKHR", (unsigned int)&eglDestroyImageKHR },
  { "eglDestroySurface", (unsigned int)&eglDestroySurface },
  { "eglDestroySyncKHR", (unsigned int)&eglDestroySyncKHR },
  { "eglDrawFrameModeDMP", (unsigned int)&eglDrawFrameModeDMP },
  { "eglGetConfigAttrib", (unsigned int)&eglGetConfigAttrib },
  { "eglGetConfigs", (unsigned int)&eglGetConfigs },
  { "eglGetCurrentContext", (unsigned int)&eglGetCurrentContext },
  { "eglGetCurrentDisplay", (unsigned int)&eglGetCurrentDisplay },
  { "eglGetCurrentSurface", (unsigned int)&eglGetCurrentSurface },
  { "eglGetDisplay", (unsigned int)&eglGetDisplay },
  { "eglGetError", (unsigned int)&eglGetError },
  { "eglGetSyncAttribKHR", (unsigned int)&eglGetSyncAttribKHR },
  { "eglInitialize", (unsigned int)&eglInitialize },
  { "eglInvalidateImageDMP", (unsigned int)&eglInvalidateImageDMP },
  { "eglMakeCurrent", (unsigned int)&eglMakeCurrent },
  { "eglQueryAPI", (unsigned int)&eglQueryAPI },
  { "eglQueryContext", (unsigned int)&eglQueryContext },
  { "eglQueryDisplayDMP", (unsigned int)&eglQueryDisplayDMP },
  { "eglQueryFrameDMP", (unsigned int)&eglQueryFrameDMP },
  { "eglQueryString", (unsigned int)&eglQueryString },
  { "eglQuerySurface", (unsigned int)&eglQuerySurface },
  { "eglQuerySurfacePointerANGLE", (unsigned int)&eglQuerySurfacePointerANGLE },
  { "eglReleaseTexImage", (unsigned int)&eglReleaseTexImage },
  { "eglReleaseThread", (unsigned int)&eglReleaseThread },
  { "eglResumeDMP", (unsigned int)&eglResumeDMP },
  { "eglSetHardwareStateDMP", (unsigned int)&eglSetHardwareStateDMP },
  { "eglSignalSyncKHR", (unsigned int)&eglSignalSyncKHR },
  { "eglSurfaceAttrib", (unsigned int)&eglSurfaceAttrib },
  { "eglSuspendDMP", (unsigned int)&eglSuspendDMP },
  { "eglSwapBuffers", (unsigned int)&eglSwapBuffers },
  { "eglSwapInterval", (unsigned int)&eglSwapInterval },
  { "eglTerminate", (unsigned int)&eglTerminate },
  { "eglWaitClient", (unsigned int)&eglWaitClient },
  { "eglWaitGL", (unsigned int)&eglWaitGL },
  { "eglWaitNative", (unsigned int)&eglWaitNative },
  { "glActiveTexture", (unsigned int)&glActiveTexture },
  { "glAttachShader", (unsigned int)&glAttachShader },
  { "glBindAttribLocation", (unsigned int)&glBindAttribLocation },
  { "glBindBuffer", (unsigned int)&glBindBuffer },
  { "glBindFramebuffer", (unsigned int)&glBindFramebuffer },
  { "glBindRenderbuffer", (unsigned int)&glBindRenderbuffer },
  { "glBindTexture", (unsigned int)&glBindTexture },
  { "glBlendColor", (unsigned int)&glBlendColor },
  { "glBlendEquation", (unsigned int)&glBlendEquation },
  { "glBlendEquationSeparate", (unsigned int)&glBlendEquationSeparate },
  { "glBlendFunc", (unsigned int)&glBlendFunc },
  { "glBlendFuncSeparate", (unsigned int)&glBlendFuncSeparate },
  { "glBufferData", (unsigned int)&glBufferData },
  { "glBufferSubData", (unsigned int)&glBufferSubData },
  { "glCheckFramebufferStatus", (unsigned int)&glCheckFramebufferStatus },
  { "glClear", (unsigned int)&glClear },
  { "glClearColor", (unsigned int)&glClearColor },
  { "glClearDepthf", (unsigned int)&glClearDepthf },
  { "glClearStencil", (unsigned int)&glClearStencil },
  { "glColorMask", (unsigned int)&glColorMask },
  { "glCompileShader", (unsigned int)&glCompileShader },
  { "glCompressedTexImage2D", (unsigned int)&glCompressedTexImage2D },
  { "glCompressedTexSubImage2D", (unsigned int)&glCompressedTexSubImage2D },
  { "glCopyTexImage2D", (unsigned int)&glCopyTexImage2D },
  { "glCopyTexSubImage2D", (unsigned int)&glCopyTexSubImage2D },
  { "glCreateProgram", (unsigned int)&glCreateProgram },
  { "glCreateShader", (unsigned int)&glCreateShader },
  { "glCullFace", (unsigned int)&glCullFace },
  { "glDeleteBuffers", (unsigned int)&glDeleteBuffers },
  { "glDeleteFramebuffers", (unsigned int)&glDeleteFramebuffers },
  { "glDeleteProgram", (unsigned int)&glDeleteProgram },
  { "glDeleteRenderbuffers", (unsigned int)&glDeleteRenderbuffers },
  { "glDeleteShader", (unsigned int)&glDeleteShader },
  { "glDeleteTextures", (unsigned int)&glDeleteTextures },
  { "glDepthFunc", (unsigned int)&glDepthFunc },
  { "glDepthMask", (unsigned int)&glDepthMask },
  { "glDepthRangef", (unsigned int)&glDepthRangef },
  { "glDetachShader", (unsigned int)&glDetachShader },
  { "glDisable", (unsigned int)&glDisable },
  { "glDisableVertexAttribArray", (unsigned int)&glDisableVertexAttribArray },
  { "glDrawArrays", (unsigned int)&glDrawArrays },
  { "glDrawElements", (unsigned int)&glDrawElements },
  { "glEGLImageTargetRenderbufferStorageOES", (unsigned int)&glEGLImageTargetRenderbufferStorageOES },
  { "glEGLImageTargetTexture2DOES", (unsigned int)&glEGLImageTargetTexture2DOES },
  { "glEnable", (unsigned int)&glEnable },
  { "glEnableVertexAttribArray", (unsigned int)&glEnableVertexAttribArray },
  { "glFinish", (unsigned int)&glFinish },
  { "glFlush", (unsigned int)&glFlush },
  { "glFramebufferRenderbuffer", (unsigned int)&glFramebufferRenderbuffer },
  { "glFramebufferTexture2D", (unsigned int)&glFramebufferTexture2D },
  { "glFrontFace", (unsigned int)&glFrontFace },
  { "glGenBuffers", (unsigned int)&glGenBuffers },
  { "glGenFramebuffers", (unsigned int)&glGenFramebuffers },
  { "glGenRenderbuffers", (unsigned int)&glGenRenderbuffers },
  { "glGenTextures", (unsigned int)&glGenTextures },
  { "glGenerateMipmap", (unsigned int)&glGenerateMipmap },
  { "glGetActiveAttrib", (unsigned int)&glGetActiveAttrib },
  { "glGetActiveUniform", (unsigned int)&glGetActiveUniform },
  { "glGetAttachedShaders", (unsigned int)&glGetAttachedShaders },
  { "glGetAttribLocation", (unsigned int)&glGetAttribLocation },
  { "glGetBooleanv", (unsigned int)&glGetBooleanv },
  { "glGetBufferParameteriv", (unsigned int)&glGetBufferParameteriv },
  { "glGetError", (unsigned int)&glGetError },
  { "glGetFloatv", (unsigned int)&glGetFloatv },
  { "glGetFramebufferAttachmentParameteriv", (unsigned int)&glGetFramebufferAttachmentParameteriv },
  { "glGetIntegerv", (unsigned int)&glGetIntegerv },
  { "glGetProgramBinaryOES", (unsigned int)&glGetProgramBinaryOES },
  { "glGetProgramInfoLog", (unsigned int)&glGetProgramInfoLog },
  { "glGetProgramiv", (unsigned int)&glGetProgramiv },
  { "glGetRenderbufferParameteriv", (unsigned int)&glGetRenderbufferParameteriv },
  { "glGetShaderInfoLog", (unsigned int)&glGetShaderInfoLog },
  { "glGetShaderPrecisionFormat", (unsigned int)&glGetShaderPrecisionFormat },
  { "glGetShaderSource", (unsigned int)&glGetShaderSource },
  { "glGetShaderiv", (unsigned int)&glGetShaderiv },
  { "glGetString", (unsigned int)&glGetString },
  { "glGetTexParameterfv", (unsigned int)&glGetTexParameterfv },
  { "glGetTexParameteriv", (unsigned int)&glGetTexParameteriv },
  { "glGetUniformLocation", (unsigned int)&glGetUniformLocation },
  { "glGetUniformfv", (unsigned int)&glGetUniformfv },
  { "glGetUniformiv", (unsigned int)&glGetUniformiv },
  { "glGetVertexAttribPointerv", (unsigned int)&glGetVertexAttribPointerv },
  { "glGetVertexAttribfv", (unsigned int)&glGetVertexAttribfv },
  { "glGetVertexAttribiv", (unsigned int)&glGetVertexAttribiv },
  { "glHint", (unsigned int)&glHint },
  { "glIsBuffer", (unsigned int)&glIsBuffer },
  { "glIsEnabled", (unsigned int)&glIsEnabled },
  { "glIsFramebuffer", (unsigned int)&glIsFramebuffer },
  { "glIsProgram", (unsigned int)&glIsProgram },
  { "glIsRenderbuffer", (unsigned int)&glIsRenderbuffer },
  { "glIsShader", (unsigned int)&glIsShader },
  { "glIsTexture", (unsigned int)&glIsTexture },
  { "glLineWidth", (unsigned int)&glLineWidth },
  { "glLinkProgram", (unsigned int)&glLinkProgram },
  { "glPixelStorei", (unsigned int)&glPixelStorei },
  { "glPolygonOffset", (unsigned int)&glPolygonOffset },
  { "glProgramBinaryOES", (unsigned int)&glProgramBinaryOES },
  { "glReadPixels", (unsigned int)&glReadPixels },
  { "glReleaseShaderCompiler", (unsigned int)&glReleaseShaderCompiler },
  { "glRenderbufferStorage", (unsigned int)&glRenderbufferStorage },
  { "glSampleCoverage", (unsigned int)&glSampleCoverage },
  { "glScissor", (unsigned int)&glScissor },
  { "glShaderBinary", (unsigned int)&glShaderBinary },
  { "glShaderSource", (unsigned int)&glShaderSource },
  { "glStencilFunc", (unsigned int)&glStencilFunc },
  { "glStencilFuncSeparate", (unsigned int)&glStencilFuncSeparate },
  { "glStencilMask", (unsigned int)&glStencilMask },
  { "glStencilMaskSeparate", (unsigned int)&glStencilMaskSeparate },
  { "glStencilOp", (unsigned int)&glStencilOp },
  { "glStencilOpSeparate", (unsigned int)&glStencilOpSeparate },
  { "glTexImage2D", (unsigned int)&glTexImage2D },
  { "glTexParameterf", (unsigned int)&glTexParameterf },
  { "glTexParameterfv", (unsigned int)&glTexParameterfv },
  { "glTexParameteri", (unsigned int)&glTexParameteri },
  { "glTexParameteriv", (unsigned int)&glTexParameteriv },
  { "glTexSubImage2D", (unsigned int)&glTexSubImage2D },
  { "glUniform1f", (unsigned int)&glUniform1f },
  { "glUniform1fv", (unsigned int)&glUniform1fv },
  { "glUniform1i", (unsigned int)&glUniform1i },
  { "glUniform1iv", (unsigned int)&glUniform1iv },
  { "glUniform2f", (unsigned int)&glUniform2f },
  { "glUniform2fv", (unsigned int)&glUniform2fv },
  { "glUniform2i", (unsigned int)&glUniform2i },
  { "glUniform2iv", (unsigned int)&glUniform2iv },
  { "glUniform3f", (unsigned int)&glUniform3f },
  { "glUniform3fv", (unsigned int)&glUniform3fv },
  { "glUniform3i", (unsigned int)&glUniform3i },
  { "glUniform3iv", (unsigned int)&glUniform3iv },
  { "glUniform4f", (unsigned int)&glUniform4f },
  { "glUniform4fv", (unsigned int)&glUniform4fv },
  { "glUniform4i", (unsigned int)&glUniform4i },
  { "glUniform4iv", (unsigned int)&glUniform4iv },
  { "glUniformMatrix2fv", (unsigned int)&glUniformMatrix2fv },
  { "glUniformMatrix3fv", (unsigned int)&glUniformMatrix3fv },
  { "glUniformMatrix4fv", (unsigned int)&glUniformMatrix4fv },
  { "glUseProgram", (unsigned int)&glUseProgram },
  { "glValidateProgram", (unsigned int)&glValidateProgram },
  { "glVertexAttrib1f", (unsigned int)&glVertexAttrib1f },
  { "glVertexAttrib1fv", (unsigned int)&glVertexAttrib1fv },
  { "glVertexAttrib2f", (unsigned int)&glVertexAttrib2f },
  { "glVertexAttrib2fv", (unsigned int)&glVertexAttrib2fv },
  { "glVertexAttrib3f", (unsigned int)&glVertexAttrib3f },
  { "glVertexAttrib3fv", (unsigned int)&glVertexAttrib3fv },
  { "glVertexAttrib4f", (unsigned int)&glVertexAttrib4f },
  { "glVertexAttrib4fv", (unsigned int)&glVertexAttrib4fv },
  { "glVertexAttribPointer", (unsigned int)&glVertexAttribPointer },
  { "glViewport", (unsigned int)&glViewport },
};
static int seq2(const char *a, const char *b){ while(*a && *a==*b){a++;b++;} return *a==*b; }
static unsigned int lookup(const char *n)
{
    unsigned int i;
    if (!n) return 0;
    for (i = 0; i < sizeof(g_tab)/sizeof(g_tab[0]); i++)
        if (seq2(g_tab[i].n, n)) return g_tab[i].f;
    rec("eglGetProcAddress-MISS", (unsigned int)n, 0, 0, 0);
    return 0;
}
unsigned int eglGetProcAddress(unsigned int a1,unsigned int a2,unsigned int a3,unsigned int a4)
{
    rec("eglGetProcAddress", a1, a2, a3, a4);
    return lookup((const char *)a1);
}

