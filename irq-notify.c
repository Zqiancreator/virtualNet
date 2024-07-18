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
static struct class*	cls=NULL;
static void __iomem*	base_address;	
static resource_size_t  remap_size;  
static int	            irq;
static int 				diff_value;
/*******************************************************************************
                              Macro definitions                               
 ******************************************************************************/
#define FIRST_IRQ		104
#define IRQ_NUM			6
#define COMPATIBLE_NAME "npc,irq"
#define DEVICE_NAME "irq_drv"
#define INTR_NAME	"irqdrv"
#define RING_BUFF_DEPTH         6
/*******************************************************************************
                        	  Global variable declarations                          
 ******************************************************************************/
static volatile int irq_is_open = 0;
static struct fasync_struct *irq_async;
static void send_notification(void);
// ringbuffer begin
	typedef struct 
	{    
		u8							  bMax;                 // 环形缓冲区的最大索引    
		u8							  bWrIx; 				// 写索引    
		u8							  bRdIx;                // 读索引    
	}Cl2_Packet_Fifo_Type;
	Cl2_Packet_Fifo_Type *ringbuffer;
	EXPORT_SYMBOL(ringbuffer);
	/*****************************************************************************
	 Prototype    : Cl2FifoPutPacket
	Description  : 
	Input        : 
	Output       : None
	Return Value : 
	Calls        : 
	Called By    : 
	
	History        :
	1.Date         : 2024/6/13
		Author       : 
		Modification : Created function

	*****************************************************************************/
	void Cl2FifoPutPacket(Cl2_Packet_Fifo_Type *psFifo)
	{    
		u8 bNextWrIx;    
		u16 delay=0;
	 
		// bNextWrIx = (psFifo->bWrIx + 1) & psFifo->bMax;
		psFifo->bWrIx = (psFifo->bWrIx + 1) & psFifo->bMax;
		/* If fifo is empty, notify c2h device*/    
		if (((psFifo->bRdIx + 1) & psFifo->bMax) == psFifo->bWrIx)    
		{        
			printk(KERN_ERR "%x %x\n", psFifo->bRdIx, psFifo->bWrIx);
			// for(delay =0;delay<10000;delay++){};
			send_notification();
		}   


		// #ifdef _DEBUG_CL2    
		// if (bNextWrIx == psFifo->bRdIx)    
		// {        
		// 	cl2break(0x1400);    
		// }
		// #endif    

		// #ifdef SHOW_CL2_FIFO    
		// cl2print(_L2, _LOG_M, 
		// 		"\n[F:0x%08x]P 0x%08x", 
		// 		(U32)psFifo, (U32)psFifo->asPkt[psFifo->bWrIx]);
		// #endif    
	}

	/*****************************************************************************
	 Prototype    : Cl2FifoCreateFifo
	Description  : 
	Input        : 
	Output       : None
	Return Value : 
	Calls        : 
	Called By    : 
	
	History        :
	1.Date         : 2024/6/13
		Author       : 
		Modification : Created function

	*****************************************************************************/
	Cl2_Packet_Fifo_Type *Cl2FifoCreateFifo(u8 bDepth) 
	{    
		Cl2_Packet_Fifo_Type *psFifo = NULL;    
		u16					  wNewDepth;    
		u16					  wBufRequired;    

		/* Choose Depth */    
		wNewDepth = 1 << bDepth;    

		wBufRequired = sizeof(Cl2_Packet_Fifo_Type) + wNewDepth * sizeof(struct scatterlist *);    
		psFifo = (Cl2_Packet_Fifo_Type *)kmalloc(wBufRequired, GFP_KERNEL);   

		if (psFifo == NULL)    
		{        
			return NULL;    
		}    

		psFifo->bMax 	= wNewDepth - 1;    
		psFifo->bWrIx 	= 0;    
		psFifo->bRdIx 	= 0;    
		    
		return psFifo;
	}

	/*****************************************************************************
	 Prototype    : Cl2FifoRemoveFifo
	Description  : 
	Input        : 
	Output       : None
	Return Value : 
	Calls        : 
	Called By    : 
	
	History        :
	1.Date         : 2024/6/13
		Author       : 
		Modification : Created function

	*****************************************************************************/
	void Cl2FifoRemoveFifo(Cl2_Packet_Fifo_Type *psFifo)
	{    
		if (psFifo == NULL)    
		{        
			return;    
		}    

		kfree(psFifo);
	}

static irqreturn_t irq_interrupt(int irq, void *dev_id)
{
	// printk("irq = %d,diff:%d,irq_is_open:%d\n", irq + diff_value,diff_value,irq_is_open);
	Cl2FifoPutPacket(ringbuffer);

	return IRQ_HANDLED;
}

// ringbuffer end

//notify begin
	// 定义一个通知链
	static RAW_NOTIFIER_HEAD(my_notifier_chain);

	// 发送通知的函数
	static void send_notification(void)
	{
		int ret;
		// 向通知链的所有注册者发送通知
        char* val = "Hello, nvif\n";
		ret = raw_notifier_call_chain(&my_notifier_chain, 0, val);
		// printk(KERN_INFO "Notification sent, return value: %d\n", ret);
	}

	// 供其他模块注册到这个通知链的接口函数
	int register_my_notifier(struct notifier_block *nb)
	{
		return raw_notifier_chain_register(&my_notifier_chain, nb);
	}
	EXPORT_SYMBOL(register_my_notifier);

	// 供其他模块从这个通知链注销的接口函数
	int unregister_my_notifier(struct notifier_block *nb)
	{
		return raw_notifier_chain_unregister(&my_notifier_chain, nb);
	}
	EXPORT_SYMBOL(unregister_my_notifier);
//notify end
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
 
 
static int irq_probe(struct platform_device *pdev)
{
	int					err;
	struct device *tmp_dev;
	int irqindex = 0;

    printk("hello intr\n");

	ringbuffer = Cl2FifoCreateFifo(RING_BUFF_DEPTH);
	if (cls) {
		printk("device exit\n");	
	} else {
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
	}

	while(irqindex < IRQ_NUM){
		irq = platform_get_irq(pdev,irqindex);
		if(irqindex == 0)
			diff_value = FIRST_IRQ - irq;
		if (irq <= 0)
			return -ENXIO;
		// printk("irq = %d\n", irq);
	
		err = request_threaded_irq(irq, NULL,
					irq_interrupt,
					IRQF_ONESHOT,
					devname, NULL);				   
		if (err) {
			printk(KERN_ALERT "irq_probe irq	error=%d\n", err);
			goto fail;
		}
		else
		{
			// printk("irq = %d\n", irq);
			// printk("devname = %s\n", devname);
		}
		irqindex++;
	}

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
	// printk("irq = %d\n", irq);
 
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
	{.compatible = COMPATIBLE_NAME },
	{ }
};
MODULE_DEVICE_TABLE(of, irq_of_match);
 
 
static struct platform_driver irq_driver = {
	.probe = irq_probe,
	.remove	= irq_remove,
	.driver = {
		.owner   		= THIS_MODULE,
		.name	 		= INTR_NAME,
		.pm    			= &irq_pm_ops,
		.of_match_table	= irq_of_match,		
	},
};
 
module_platform_driver(irq_driver);
MODULE_LICENSE("GPL v2");
