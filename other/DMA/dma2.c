#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/async_tx.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <linux/io.h>

#define DEVICE_NAME "ax_dma"
#define MAX_SIZE (512 * 64)

#define SRC_PHYS_ADDR 0x0  // 源物理地址
#define DST_PHYS_ADDR 0x10  // 目标物理地址

static char *src_virt;
static char *dst_virt;
dma_addr_t dma_src;
dma_addr_t dma_dst;

struct ax_dma_drv
{
    struct dma_chan *chan;
    struct dma_device *dev;
    struct dma_async_tx_descriptor *tx;
    enum dma_ctrl_flags flags;
    dma_cookie_t cookie;
};
struct ax_dma_drv ax_dma;

void dma_cb(void *dma_async_param)
{
    if (!memcmp(src_virt, dst_virt, MAX_SIZE))
    {
        printk("dma irq test ok\n");
    }
    else
    {
        printk("dma irq test failed\n");
    }
}

static int dma_open(struct inode *inode, struct file *file)
{
    printk("dma_open\n");
    return 0;
}

static int dma_release(struct inode *inode, struct file *file)
{
    printk("dma_release\n");
    return 0;
}

static ssize_t dma_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    int ret = 0;
    printk("dma_read\n");

    ax_dma.tx = ax_dma.dev->device_prep_dma_memcpy(ax_dma.chan, dma_dst, dma_src, MAX_SIZE, ax_dma.flags);
    if (!ax_dma.tx)
    {
        printk(KERN_INFO "Failed to prepare DMA memcpy\n");
        return -EFAULT;
    }

    ax_dma.tx->callback = dma_cb;
    ax_dma.tx->callback_param = NULL;
    ax_dma.cookie = ax_dma.tx->tx_submit(ax_dma.tx);
    if (dma_submit_error(ax_dma.cookie))
    {
        printk("DMA tx submit failed\n");
        return -EIO;
    }
    dma_async_issue_pending(ax_dma.chan);

    return ret;
}

static struct file_operations ax_fops =
{
    .owner = THIS_MODULE,
    .open = dma_open,
    .read = dma_read,
    .release = dma_release,
};

static struct miscdevice dma_misc =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &ax_fops,
};

static int __init dma_init(void)
{
    int ret = 0;
    dma_cap_mask_t mask;

    ret = misc_register(&dma_misc);
    if (ret)
    {
        printk("misc_register failed!\n");
        return ret;
    }
    printk("drv register ok\n");
    of_dma_configure(dma_misc.this_device, dma_misc.this_device->of_node, true);
    dma_misc.this_device->coherent_dma_mask = 0xffffffff;

    // 将物理地址映射到虚拟地址空间
    src_virt = ioremap(SRC_PHYS_ADDR, MAX_SIZE);
    if (!src_virt)
    {
        printk("Failed to map source physical address\n");
        return -ENOMEM;
    }

    dst_virt = ioremap(DST_PHYS_ADDR, MAX_SIZE);
    if (!dst_virt)
    {
        printk("Failed to map destination physical address\n");
        iounmap(src_virt);
        return -ENOMEM;
    }

    // 将源地址映射到 DMA 地址空间
    dma_src = dma_map_single(dma_misc.this_device, src_virt, MAX_SIZE, DMA_TO_DEVICE);
    if (dma_mapping_error(dma_misc.this_device, dma_src))
    {
        printk("Failed to map source memory to DMA address\n");
        iounmap(src_virt);
        iounmap(dst_virt);
        return -EFAULT;
    }

    // 将目标地址映射到 DMA 地址空间
    dma_dst = dma_map_single(dma_misc.this_device, dst_virt, MAX_SIZE, DMA_FROM_DEVICE);
    if (dma_mapping_error(dma_misc.this_device, dma_dst))
    {
        printk("Failed to map destination memory to DMA address\n");
        dma_unmap_single(dma_misc.this_device, dma_src, MAX_SIZE, DMA_TO_DEVICE);
        iounmap(src_virt);
        iounmap(dst_virt);
        return -EFAULT;
    }

    // 初始化mask
    dma_cap_zero(mask);
    dma_cap_set(DMA_MEMCPY, mask);
    ax_dma.chan = dma_request_channel(mask, NULL, NULL);
    ax_dma.flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
    ax_dma.dev = ax_dma.chan->device;
    printk("chan request ok\n");

    // 给源地址一个初值
    memset(src_virt, 0x5A, MAX_SIZE);
    // 给目标地址一个不一样的初值
    memset(dst_virt, 0x6A, MAX_SIZE);

    return 0;
}

static void __exit dma_exit(void)
{
    dma_unmap_single(dma_misc.this_device, dma_src, MAX_SIZE, DMA_TO_DEVICE);
    dma_unmap_single(dma_misc.this_device, dma_dst, MAX_SIZE, DMA_FROM_DEVICE);

    dma_release_channel(ax_dma.chan);

    iounmap(src_virt);
    iounmap(dst_virt);

    dma_free_coherent(dma_misc.this_device, MAX_SIZE, src_virt, dma_src);
    dma_free_coherent(dma_misc.this_device, MAX_SIZE, dst_virt, dma_dst);

    misc_deregister(&dma_misc);
}

// 驱动入口函数标记
module_init(dma_init);
// 驱动出口函数标记
module_exit(dma_exit);

/* 驱动描述信息 */
MODULE_AUTHOR("Alinx");
MODULE_ALIAS("dma");
MODULE_DESCRIPTION("DMA driver");
MODULE_VERSION("v1.0");
MODULE_LICENSE("GPL");
