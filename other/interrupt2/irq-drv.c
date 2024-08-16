#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/delay.h>
 
#include <linux/dma-mapping.h>
 
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/dma-buf.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/init.h>
 
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/errno.h>	/* error codes */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
 
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pci.h>
 
#include <linux/time.h>
#include <linux/timer.h>
 
//
static char 			devname[16];
static int 				major;
static int             	mijor;
static struct class*	cls;
static void __iomem*	base_address;	
static resource_size_t  remap_size;  
static int	            irq;
static struct device*	dev;           
 
#define DEVICE_NAME "irq_drv"
static volatile int irq_is_open = 0;
static struct fasync_struct *irq_async;
 
static int irq_drv_open(struct inode *Inode, struct file *File)
{
	irq_is_open = 1;
	return 0;
}
 
int irq_drv_release (struct inode *inode, struct file *file)
{
	irq_is_open = 0;
	return 0;
}
 
static ssize_t irq_drv_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}
 
static ssize_t irq_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}
 
static int irq_drv_fasync (int fd, struct file *filp, int on)
{
	return fasync_helper (fd, filp, on, &irq_async);
}
 
static struct file_operations irq_fops = {	
	.owner  		= THIS_MODULE,
	.open 			= irq_drv_open,
	.read 			= irq_drv_read, 
	.write 			= irq_drv_write,
	.fasync		 	= irq_drv_fasync,
	.release		= irq_drv_release,
};
 
static irqreturn_t irq_interrupt(int irq, void *dev_id)
{
	printk("irq = %d\n", irq);
	if(irq_is_open)
	{
// 发送异步信号 &irq_async：目标fasync_struct 结构体变量指针的地址。SIGIO：发送的信号类型，范围是文件arch/xtensa/include/uapi/asm/signal.h 的 34~72 行宏
// band：设备可写时设为POLL_IN，可读时为POLL_OUT
		kill_fasync (&irq_async, SIGIO, POLL_IN);
	}
	return IRQ_HANDLED;
}
 
static int irq_probe(struct platform_device *pdev)
{
    printk("hello intr\n");
	int					err;
	struct device *tmp_dev;
	memset(devname,0,16);
	strcpy(devname, DEVICE_NAME);
	
	major = register_chrdev(0, devname, &irq_fops);
 
	cls = class_create(THIS_MODULE, devname);
	mijor = 1;
	tmp_dev = device_create(cls, &pdev->dev, MKDEV(major, mijor), NULL, devname);
	if (IS_ERR(tmp_dev)) {
		class_destroy(cls);
		unregister_chrdev(major, devname);
		return 0;
	}
	// 另一种获取函数，尚不明确
	// irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	// if (irq <= 0)
	// 	return -ENXIO;
    // printk("irq1 = %d pdev->dev:%d\n", irq,pdev->dev);

	irq = platform_get_irq(pdev,0);
	if (irq <= 0)
		return -ENXIO;
    printk("irq = %d\n", irq);
	dev = &pdev->dev;
    
 
	err = request_threaded_irq(irq, NULL,
				irq_interrupt,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				devname, NULL);				   
	if (err) {
		printk(KERN_ALERT "irq_probe irq	error=%d\n", err);
		goto fail;
	}
	else
	{
		printk("irq = %d\n", irq);
		printk("devname = %s\n", devname);
	}
 
	//保存dev
	//platform_set_drvdata(pdev, &xxx);	

	return 0;
 
fail:
	
	free_irq(irq, NULL);
 
	device_destroy(cls, MKDEV(major, mijor));	
	class_destroy(cls);
	unregister_chrdev(major, devname);
 
	return -ENOMEM;
 
}
 
static int irq_remove(struct platform_device *pdev)
{
	device_destroy(cls, MKDEV(major, mijor));	
	class_destroy(cls);
	unregister_chrdev(major, devname);
	
	free_irq(irq, NULL);
	printk("irq = %d\n", irq);
 
	return 0;
}
 
static int irq_suspend(struct device *dev)
{
	return 0;
}
 
static int irq_resume(struct device *dev)
{
	return 0;
}
 
static const struct dev_pm_ops irq_pm_ops = {
	.suspend = irq_suspend,
	.resume  = irq_resume,
};
 
 
//MODULE_DEVICE_TABLE(platform, irq_driver_ids);
 
static const struct of_device_id irq_of_match[] = {
	{.compatible = "hello,irq" },
	{ }
};
MODULE_DEVICE_TABLE(of, irq_of_match);
 
 
static struct platform_driver irq_driver = {
	.probe = irq_probe,
	.remove	= irq_remove,
	.driver = {
		.owner   		= THIS_MODULE,
		.name	 		= "irq",
		.pm    			= &irq_pm_ops,
		.of_match_table	= irq_of_match,		
	},
};
 
module_platform_driver(irq_driver);
MODULE_LICENSE("GPL v2");
