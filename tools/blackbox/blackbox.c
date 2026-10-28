/* blackbox.ko —— 启动黑匣子（内核态部分）
 * 价值：用户态死掉/卡死之后它仍然活着 —— 心跳时间线可以回答
 *       「im.elf 还在不在」「系统是 hang 了还是 panic 了」。
 * 写法刻意保守：只用一个 kthread + filp_open/vfs_write，不碰锁、不碰 RCU、
 * 不注册 notifier，避免 RT 内核上的结构体布局风险。
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>

#define BB_BASE   8192       /* bb.so 用前 8 KB（8 个实例槽），模块从 8 KB 起 */
#define BB_SLOTS  200        /* 心跳记录条数 */
#define BB_REC    64         /* 每条 64 字节 */

static char *dev = "/dev/bb17";
module_param(dev, charp, 0644);
MODULE_PARM_DESC(dev, "raw block device to record boot trace");

static struct task_struct *bb_thread;
static struct file *bb_filp;
static int bb_seq;
static volatile int bb_stop;

static int bb_open(void)
{
    bb_filp = filp_open(dev, O_RDWR | O_LARGEFILE, 0);
    if (IS_ERR(bb_filp)) {
        printk(KERN_ERR "bbmod: cannot open %s (%ld)\n", dev, PTR_ERR(bb_filp));
        bb_filp = NULL;
        return -1;
    }
    return 0;
}

/* 在 offset 处写 n 字节（内核缓冲） */
static int bb_pwrite(loff_t off, const char *buf, int n)
{
    mm_segment_t old;
    int r;
    if (!bb_filp) return -1;
    old = get_fs();
    set_fs(KERNEL_DS);
    bb_filp->f_pos = off;
    r = vfs_write(bb_filp, buf, n, &bb_filp->f_pos);
    set_fs(old);
    return (r == n) ? 0 : -1;
}

static int bb_count_imelf(void)
{
    struct task_struct *p;
    int n = 0;
    for_each_process(p) {
        if (p->comm[0] == 'i' && p->comm[1] == 'm' && p->comm[2] == '.' &&
            p->comm[3] == 'e' && p->comm[4] == 'l' && p->comm[5] == 'f')
            n++;
    }
    return n;
}

static int bb_run(void *arg)
{
    char rec[BB_REC];
    (void)arg;
    while (!bb_stop && !kthread_should_stop()) {
        int n = bb_count_imelf();
        int len = 0;
        /* 手工拼一行，避免 snprintf 的浮点/格式化开销 */
        len += scnprintf(rec + len, BB_REC - len, "T %lu imelf=%d\n",
                         (unsigned long)jiffies, n);
        bb_pwrite(BB_BASE + (bb_seq % BB_SLOTS) * BB_REC, rec, len);
        bb_seq++;
        msleep_interruptible(500);
    }
    return 0;
}

static int __init bbmod_init(void)
{
    char hdr[64];
    int len;
    if (bb_open() != 0) return -ENODEV;
    len = scnprintf(hdr, sizeof hdr, "BBMOD1 loaded jiffies=%lu\n",
                    (unsigned long)jiffies);
    bb_pwrite(BB_BASE + BB_SLOTS * BB_REC, hdr, len);   /* 头部放在心跳区之后 */
    bb_thread = kthread_run(bb_run, NULL, "bbmod");
    if (IS_ERR(bb_thread)) {
        bb_thread = NULL;
        printk(KERN_ERR "bbmod: kthread_run failed\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "bbmod: started, dev=%s\n", dev);
    return 0;
}

static void __exit bbmod_exit(void)
{
    char t[32];
    int len;
    bb_stop = 1;
    if (bb_thread) { kthread_stop(bb_thread); bb_thread = NULL; }
    len = scnprintf(t, sizeof t, "BBMOD1 unloaded\n");
    bb_pwrite(BB_BASE + BB_SLOTS * BB_REC + 64, t, len);
    if (bb_filp) { filp_close(bb_filp, NULL); bb_filp = NULL; }
    printk(KERN_INFO "bbmod: stopped\n");
}

module_init(bbmod_init);
module_exit(bbmod_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("boot blackbox: heartbeat + im.elf liveness to raw block dev");
