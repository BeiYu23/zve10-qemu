/* QEMU software dmpgles2 shim.
 * v73: per-open DMA coherent buffers + device (VM_IO) mapping, no kernel-RAM writes.
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define FAKE_MAJOR 248
#define FAKE_MINOR 29
#define FB_ORDER 6
#define FB_BYTES (1u << (PAGE_SHIFT + FB_ORDER))
#define MAX_CTX 8

static dev_t fake_dev = MKDEV(FAKE_MAJOR, FAKE_MINOR);
static struct cdev fake_cdev;
static atomic_t opens = ATOMIC_INIT(0);
static atomic_t ioctl_logs = ATOMIC_INIT(0);
static DEFINE_MUTEX(ctx_lock);

struct fake_ctx {
    void *buf;
    dma_addr_t phys;
    int used;
    int is_dma;
};
static struct fake_ctx ctxs[MAX_CTX];

static const u32 fake_state[] = {
    0x80000000U, /* index 0 */
    0x50000000U, /* index 1: GPE register area (unassigned hole) */
};

static struct fake_ctx *ctx_find_used(struct file *f)
{
    struct fake_ctx *c = f->private_data;
    if (!c || !c->used) return NULL;
    return c;
}

static int fake_open(struct inode *ino, struct file *f)
{
    int i, r;
    struct fake_ctx *c = NULL;
    mutex_lock(&ctx_lock);
    for (i = 0; i < MAX_CTX; i++) {
        if (!ctxs[i].used) { c = &ctxs[i]; break; }
    }
    if (!c) { mutex_unlock(&ctx_lock); return -ENOMEM; }
    memset(c, 0, sizeof(*c));
    c->buf = dma_alloc_coherent(NULL, FB_BYTES, &c->phys, GFP_KERNEL);
    c->is_dma = (c->buf != NULL);
    if (!c->buf) {
        /* fallback: contiguous low pages */
        c->buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, FB_ORDER);
        if (!c->buf) { mutex_unlock(&ctx_lock); return -ENOMEM; }
        c->phys = (dma_addr_t)virt_to_phys(c->buf);
    }
    c->used = 1;
    f->private_data = c;
    r = atomic_inc_return(&opens);
    mutex_unlock(&ctx_lock);
    printk(KERN_INFO "FAKEGLES open(%d) ctx=%d buf=%p phys=0x%llx bytes=%u\n",
           r, (int)(c - ctxs), c->buf, (unsigned long long)c->phys, (unsigned)FB_BYTES);
    return 0;
}

static int fake_release(struct inode *ino, struct file *f)
{
    struct fake_ctx *c = f->private_data;
    mutex_lock(&ctx_lock);
    if (c && c->used) {
        /* TEST: defer free (keep pages) to see if grm_ma poison is due to free-then-reuse */
        c->used = 0;
        f->private_data = NULL;
    }
    mutex_unlock(&ctx_lock);
    return 0;
}

static long fake_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    unsigned int w[8] = {0,0,0,0,0,0,0,0};
    int copied = 0;
    if (arg && access_ok(VERIFY_READ, (void __user *)arg, sizeof(w)))
        copied = (copy_from_user(w, (void __user *)arg, sizeof(w)) == 0);
    (void)copied;

    if (cmd == 0xc0048202U) {
        u32 value = 0;
        u32 index = copied ? w[0] : 1;
        if (index < ARRAY_SIZE(fake_state)) value = fake_state[index];
        if (arg && copy_to_user((void __user *)arg, &value, sizeof(value))) return -EFAULT;
    } else if (cmd == 0xc0048211U || cmd == 0xc0048212U) {
        /* wait-for-GPE-operation-done: write back 0 (done) */
        u32 done = 0;
        if (arg && copy_to_user((void __user *)arg, &done, sizeof(done))) return -EFAULT;
    } else if (cmd == 0x80048206U) {
        u32 value = 0x1000;
        if (arg && copy_to_user((void __user *)arg, &value, sizeof(value))) return -EFAULT;
    } else if (cmd == 0x80048263U) {
        u32 value = 0;
        if (arg && copy_to_user((void __user *)arg, &value, sizeof(value))) return -EFAULT;
    }

    if (atomic_inc_return(&ioctl_logs) <= 64)
        printk(KERN_INFO "FAKEGLES ioctl cmd=0x%08x arg=0x%lx w=%08x,%08x,%08x,%08x\n",
               cmd, arg, w[0],w[1],w[2],w[3]);
    return 0;
}

static int fake_mmap(struct file *f, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    struct fake_ctx *c = ctx_find_used(f);
    unsigned long pfn;
    if (!c) return -ENODEV;
    if (size > FB_BYTES) return -EINVAL;
    pfn = (unsigned long)(c->phys >> PAGE_SHIFT);
    /* device-like mapping: guest must not treat these as normal RAM */
    vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
    return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static const struct file_operations fake_fops = {
    .owner = THIS_MODULE,
    .open = fake_open,
    .release = fake_release,
    .unlocked_ioctl = fake_ioctl,
    .mmap = fake_mmap,
};


/* ---- DMM service responder (qid 0x0061003C) baked into this module ---- */
#define DMM_QID   0x0061003Cu
#define MA_QID    0x014C0424u
#define DMM_POOL_PHYS 0x20312000u
#define DMM_POOL_SIZE 0x02000000u
extern int osal_reg_msg_queue_cb(unsigned id, void **handle, void *cb, unsigned long arg);
extern int osal_valloc_msg_wait(unsigned id, void **msg, unsigned size, unsigned flag);
extern int osal_snd_msg(unsigned id, void *msg);
static void *dmm_h;
static int dmm_n;

static int fake_dmm_cb(void *req, void *ctx)
{
	void *m = NULL; unsigned char *q; unsigned short seq = 0; int r;
	dmm_n++;
	if (req) seq = *(unsigned short *)((unsigned char *)req + 0x14);
	printk(KERN_INFO "FAKEDMM req n=%d w0=%08x seq=%u\n", dmm_n,
	       req ? *(unsigned *)req : 0, seq);
	r = osal_valloc_msg_wait(DMM_QID, &m, 24, 1);
	if (r || !m) { printk(KERN_INFO "FAKEDMM valloc rc=%d\n", r); (void)ctx; return 0; }
	q = (unsigned char *)m;
	memset(q, 0, 24);
	*(unsigned *)(q + 0) = 0x8001u;
	*(unsigned short *)(q + 4) = 1;
	*(unsigned short *)(q + 6) = seq;
	/* grm_ma client reads reply+0x10 as PHYSICAL and reply+0x14 as SIZE
	 * (ma_dmm_open_send_recv -> ma_dmm_open_impl -> ma_list_add ->
	 *  ma_create_mspace_wrapper).  The previous order produced the
	 *  observed QMMAP len=0x20312000 off=0x2000000. */
	*(unsigned *)(q + 0x10) = DMM_POOL_PHYS;
	*(unsigned *)(q + 0x14) = DMM_POOL_SIZE;
	r = osal_snd_msg(MA_QID, m);
	printk(KERN_INFO "FAKEDMM reply seq=%u snd=%d\n", seq, r);
	(void)ctx;
	return 0;
}


/* ---- dynamic id diagnostics: dump the two management-area candidates ---- */
/* osal_dynamic_id(unsigned, unsigned, unsigned *) is a 3-argument
 * kernel routine; the old 2-argument diagnostic here was an ABI violation
 * (allocation stores through an uninitialised r2), so it is removed. */
extern unsigned __k_4u_dynamic_id_tbls[8];  /* may not exist; use offsets instead */

static int __init fake_init(void)
{
    int r;
    r = register_chrdev_region(fake_dev, 1, "dmpgles2");
    if (r) return r;
    cdev_init(&fake_cdev, &fake_fops);
    fake_cdev.owner = THIS_MODULE;
    r = cdev_add(&fake_cdev, fake_dev, 1);
    if (r) { unregister_chrdev_region(fake_dev, 1); return r; }
    printk(KERN_INFO "INOFF inode_i_cdev=%u inode_size=%u file_private_data=%u file_size=%u inode_i_private=%u\n",
           (unsigned)offsetof(struct inode, i_cdev), (unsigned)sizeof(struct inode),
           (unsigned)offsetof(struct file, private_data), (unsigned)sizeof(struct file),
           (unsigned)offsetof(struct inode, i_private));
    { int rr = osal_reg_msg_queue_cb(DMM_QID, &dmm_h, (void *)&fake_dmm_cb, 0);
      printk(KERN_INFO "FAKEDMM reg rc=%d h=%p\n", rr, dmm_h); }
    printk(KERN_INFO "FAKEGLES init major=%d minor=%d ctx=per-open bytes=%u\n",
           FAKE_MAJOR, FAKE_MINOR, (unsigned)FB_BYTES);
    return 0;
}

static void __exit fake_exit(void)
{
    int i;
    cdev_del(&fake_cdev); unregister_chrdev_region(fake_dev, 1);
    mutex_lock(&ctx_lock);
    for (i = 0; i < MAX_CTX; i++) {
        if (ctxs[i].used) {
            if (ctxs[i].is_dma) dma_free_coherent(NULL, FB_BYTES, ctxs[i].buf, ctxs[i].phys);
            else free_pages((unsigned long)ctxs[i].buf, FB_ORDER);
            ctxs[i].is_dma = 0;
            ctxs[i].used = 0;
        }
    }
    mutex_unlock(&ctx_lock);
}
module_init(fake_init); module_exit(fake_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QEMU software dmpgles2 shim");
