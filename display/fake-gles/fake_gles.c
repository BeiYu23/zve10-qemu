#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>

#define FAKE_MAJOR 248
#define FAKE_MINOR 29
#define FB_ORDER 4
#define FB_BYTES (PAGE_SIZE << FB_ORDER)

static dev_t fake_dev = MKDEV(FAKE_MAJOR, FAKE_MINOR);
static struct cdev fake_cdev;
static void *fake_fb;
static atomic_t opens = ATOMIC_INIT(0);

static int fake_open(struct inode *ino, struct file *f)
{
    atomic_inc(&opens);
    f->private_data = fake_fb;
    printk(KERN_INFO "FAKEGLES open count=%d\\n", atomic_read(&opens));
    return 0;
}
static int fake_release(struct inode *ino, struct file *f) { return 0; }
static long fake_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    { unsigned int w[8] = {0,0,0,0,0,0,0,0};
      if (arg && access_ok(VERIFY_READ, (void __user *)arg, sizeof(w)))
          copy_from_user(w, (void __user *)arg, sizeof(w));
      printk(KERN_INFO "FAKEGLES ioctl cmd=0x%08x arg=0x%lx w=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\\n",
             cmd, arg, w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]); }
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
    printk(KERN_INFO "FAKEGLES init major=%d minor=%d fb=%p bytes=%u\\n", FAKE_MAJOR, FAKE_MINOR, fake_fb, FB_BYTES);
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
