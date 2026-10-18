#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define FAKE_MAJOR 248
#define FAKE_MINOR 29
#define FB_ORDER 4
#define FB_BYTES (PAGE_SIZE << FB_ORDER)
#define CXD_DDR_BASE 0x00000000UL
#define VIP_BASE 0x79800000UL
#define VIP_FB_OFF 0x01000000UL
#define VIP_W 320
#define VIP_H 240

static dev_t fake_dev = MKDEV(FAKE_MAJOR, FAKE_MINOR);
static struct cdev fake_cdev;
static void *fake_fb;
static atomic_t opens = ATOMIC_INIT(0);
static atomic_t ioctl_logs = ATOMIC_INIT(0);
static const u32 fake_state[] = {
    0x80000000U, /* index 0: unsupported */
    0x00000001U, /* index 1: default resource/state handle */
};

static void fake_vip_menu(void)
{
    void __iomem *fb = ioremap(CXD_DDR_BASE + VIP_FB_OFF, VIP_W * VIP_H * 2);
    void __iomem *vip = ioremap(VIP_BASE, 0x1000);
    int x, y;
    if (!fb || !vip) return;
    for (y = 0; y < VIP_H; y++) {
        for (x = 0; x < VIP_W; x++) {
            u16 c = 0x111f;
            if (x > 32 && x < 288 && y > 28 && y < 212)
                c = ((x / 32 + y / 24) & 1) ? 0x124f : 0x245f;
            if (y > 48 && y < 72 && x > 48 && x < 272)
                c = 0x0f0f;
            if (y > 88 && y < 96 && x > 64 && x < 256)
                c = 0xffff;
            if (y > 128 && y < 136 && x > 64 && x < 224)
                c = 0xffff;
            writew(c, fb + (y * VIP_W + x) * 2);
        }
    }
    /* VIP channel 2 is layer 1: RGBA4444, 320x240, DDR-relative address. */
    writel(1, vip + 0x300 + 0x00);
    writel(VIP_FB_OFF, vip + 0x300 + 0x20);
    writel(VIP_W * 2, vip + 0x300 + 0x24);
    writel(VIP_H - 1, vip + 0x300 + 0x2c);
    writel(1, vip + 0x1fc);
    printk(KERN_INFO "FAKEGLES VIP test frame programmed\\n");
}

static int fake_open(struct inode *ino, struct file *f)
{
    atomic_inc(&opens);
    f->private_data = fake_fb;
    fake_vip_menu();
    printk(KERN_INFO "FAKEGLES open count=%d\\n", atomic_read(&opens));
    return 0;
}
static int fake_release(struct inode *ino, struct file *f) { return 0; }
static long fake_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    unsigned int w[8] = {0,0,0,0,0,0,0,0};
    int copied = 0;

    if (arg && access_ok(VERIFY_READ, (void __user *)arg, sizeof(w)))
        copied = (copy_from_user(w, (void __user *)arg, sizeof(w)) == 0);

    static int frame_done;
    if (!frame_done) { frame_done = 1; fake_vip_menu(); }

    if (cmd == 0xc0048202U) {
        u32 value = 0;
        u32 index = copied ? w[0] : 1;
        if (index < ARRAY_SIZE(fake_state))
            value = fake_state[index];
        if (arg && copy_to_user((void __user *)arg, &value, sizeof(value)))
            return -EFAULT;
        w[0] = value;
    } else if (cmd == 0x80048206U || cmd == 0x80048263U) {
        u32 value = 0;
        if (arg && copy_to_user((void __user *)arg, &value, sizeof(value)))
            return -EFAULT;
        w[0] = value;
    }

    if (atomic_inc_return(&ioctl_logs) <= 64)
        printk(KERN_INFO "FAKEGLES ioctl cmd=0x%08x arg=0x%lx w=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\\n",
               cmd, arg, w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
    return 0;
}
static int fake_mmap(struct file *f, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;
    if (!fake_fb || size > FB_BYTES) return -EINVAL;
    pfn = virt_to_phys(fake_fb) >> PAGE_SHIFT;
    vma->vm_flags |= VM_RESERVED;
    return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}
static const struct file_operations fake_fops = {
    .owner = THIS_MODULE,
    .open = fake_open,
    .release = fake_release,
    .unlocked_ioctl = fake_ioctl,
    .mmap = fake_mmap,
};
static int __init fake_init(void)
{
    int r;
    fake_fb = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, FB_ORDER);
    if (!fake_fb) return -ENOMEM;
    r = register_chrdev_region(fake_dev, 1, "dmpgles2");
    if (r) { free_pages((unsigned long)fake_fb, FB_ORDER); return r; }
    cdev_init(&fake_cdev, &fake_fops);
    fake_cdev.owner = THIS_MODULE;
    r = cdev_add(&fake_cdev, fake_dev, 1);
    if (r) { unregister_chrdev_region(fake_dev, 1); free_pages((unsigned long)fake_fb, FB_ORDER); return r; }
    printk(KERN_INFO "FAKEGLES init major=%d minor=%d fb=%p bytes=%u\\n", FAKE_MAJOR, FAKE_MINOR, fake_fb, (unsigned)FB_BYTES);
    /* The VIP test frame is triggered by the first display ioctl, after QEMU devices are ready. */
    return 0;
}
static void __exit fake_exit(void)
{
    cdev_del(&fake_cdev); unregister_chrdev_region(fake_dev, 1);
    if (fake_fb) free_pages((unsigned long)fake_fb, FB_ORDER);
}
module_init(fake_init); module_exit(fake_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QEMU software dmpgles2 shim");
