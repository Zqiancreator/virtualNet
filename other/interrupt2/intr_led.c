#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
 #include <linux/gpio/consumer.h>
 #include <linux/delay.h>

 #define CHAR_DEV_NAME "pl_key_dev"

 struct alinx_char_dev{
 dev_t devid;
 struct cdev cdev;
 struct class *class;
 struct device *device;
 struct device_node *nd;
 struct gpio_desc *gpio_d;
 struct semaphore lock;
 unsigned int irq;
 struct timer_list timer;
 char key_state;
 };

 static struct alinx_char_dev alinx_char = {
 .cdev = {
 .owner = THIS_MODULE,
 },
 };

 static irqreturn_t key_handler(int irq, void *dev)
 {
  mod_timer(&alinx_char.timer, jiffies +msecs_to_jiffies(10));
  return IRQ_RETVAL(IRQ_HANDLED);
 }

 void timer_function(struct timer_list *timer)
 {
 alinx_char.key_state =1;
 }

 static int char_dev_open(struct inode*inode_p, struct file *file_p)
 {
 int ret = 0;

 ret =down_interruptible(&alinx_char.lock);
 if(ret)
 {
 printk("%s wait resource break\n",CHAR_DEV_NAME);
 ret =-1;
 }
 else
 {
 printk("char dev open\n");
 }

 return ret;
 }

 static int char_dev_release(struct inode *inode_p, struct file *file_p)
 {
 up(&alinx_char.lock);
 printk("char dev release\n");
 return 0;
 }

 static ssize_t char_dev_read(struct file *file_p, char __user *buf, size_t len, loff_t *loff_t_p)
 {
 int ret = 0;

 if(alinx_char.key_state)
 {
 ret = copy_to_user(buf, &alinx_char.key_state, sizeof(alinx_char.key_state));
 alinx_char.key_state = 0;
 }
 else
 {
 ret = copy_to_user(buf, &alinx_char.key_state, sizeof(alinx_char.key_state));
 }

 return ret;
 }

 static struct file_operations char_dev_opt = {
 .owner = THIS_MODULE,
 .open = char_dev_open,
 .read = char_dev_read,
 .release =char_dev_release,
 };

 static int __init char_drv_init(void)
 {
  int ret = 0;
  printk("begin\r\n");
/* 申请设备号 存放&alinx_char.devid*/
  ret =alloc_chrdev_region(&alinx_char.devid, 0, 1, CHAR_DEV_NAME);
  if(ret < 0)
  {
    goto err;
  }
  printk("device num alinx_char.devid =  %d \n", alinx_char.devid);
// 初始化cdev  cdev：字符设备结构体指针； fops：设备操作函数集合结构体指针。
  cdev_init(&alinx_char.cdev, &char_dev_opt);
// 注册字符设备  p：上面初始化后的字符设备结构体变量；dev：设备号；count：需要添加的设备数量。
  ret =cdev_add(&alinx_char.cdev, alinx_char.devid, 1);
  if(ret < 0)
  {
    goto err;
  }
// 创建类，在类下创建设备
  alinx_char.class = class_create(THIS_MODULE, CHAR_DEV_NAME);
  if(IS_ERR(alinx_char.class))
  {
    ret = PTR_ERR(alinx_char.class);
    goto err;
  }
// class：上面的类，设备会在这个类下创建；parent：父设备，没有填NULL； devt：设备号；
// drvdata：设备可能会用到的数据，没有的话填NULL；fmt：设备名，比如当fmt=axled时，创建设备后就会生成/dev/axled文件。
  alinx_char.device = device_create(alinx_char.class, NULL, alinx_char.devid, NULL, CHAR_DEV_NAME);
  if(IS_ERR(alinx_char.device))
  {
    ret = PTR_ERR(alinx_char.device);
    goto err;
  }

  alinx_char.device->of_node = of_find_node_by_path("/alinxkeypl");
// dev：对应的设备，of_node成员必须赋值，对应的设备树节点中必须要有“xxx-gpios”这个属性。con_id：是一个字符串，必须要是对应的设备树节点中必须要有“xxx-gpios”这个属性中“xxx”。
// idx：需要获取的gpio在节点中的下标，从0开始。flags：gpio 默认状态
// 通过这个函数的返回值设置和控制gpio
  alinx_char.gpio_d = gpiod_get_index(alinx_char.device, "key", 0, GPIOD_IN);
  gpiod_direction_input(alinx_char.gpio_d);

  alinx_char.irq = gpiod_to_irq(alinx_char.gpio_d);
  printk("irq id:%d",alinx_char.irq);
  //中断号 服务程序 触发方式 中断名 dev:flag设置 IRQF_SHARED 时，使用dev 来区分不同的设备，dev的值会传递给中断服务
  ret =request_irq(alinx_char.irq, key_handler, IRQF_TRIGGER_RISING, "alinxkey", NULL);
  if(ret < 0)
  {
    printk("irq %d request failed\r\n", alinx_char.irq);
    return-EFAULT;
  }

  timer_setup(&alinx_char.timer, timer_function, 0);
  add_timer(&alinx_char.timer);

  alinx_char.key_state =0;

  sema_init(&alinx_char.lock, 1);

err:
 return ret;
}

static void __exit char_drv_exit(void)
{
 del_timer(&alinx_char.timer);

 free_irq(alinx_char.irq, NULL);

 gpiod_put(alinx_char.gpio_d);

 cdev_del(&alinx_char.cdev);

 device_destroy(alinx_char.class, alinx_char.devid);

 class_destroy(alinx_char.class);

 unregister_chrdev_region(alinx_char.devid, 1);
}



module_init(char_drv_init);
module_exit(char_drv_exit);

MODULE_AUTHOR("Alinx");
MODULE_ALIAS("gpio_led");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_VERSION("v1.0");
MODULE_LICENSE("GPL");