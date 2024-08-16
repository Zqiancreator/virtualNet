#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>

static irqreturn_t irq_handler(int irq, void *dev_id)
{
    printk("中断处理函数被调用, IRQ: %d\n", irq);
    return IRQ_HANDLED;
}

static int __init my_module_init(void)
{
    struct device_node *node;
    int irq1, irq2;

    // 查找设备树节点
    node = of_find_compatible_node(NULL, NULL, "my,device");
    if (!node) {
        printk("未找到设备树节点\n");
        return -ENODEV;
    }

    // 获取并注册第一个中断
    irq1 = irq_of_parse_and_map(node, 0);
    if (request_irq(irq1, irq_handler, IRQF_TRIGGER_RISING, "my_device_irq1", NULL)) {
        printk("无法注册第一个中断\n");
        return -EIO;
    }

    // 获取并注册第二个中断
    irq2 = irq_of_parse_and_map(node, 1);
    if (request_irq(irq2, irq_handler, IRQF_TRIGGER_RISING, "my_device_irq2", NULL)) {
        printk("无法注册第二个中断\n");
        free_irq(irq1, NULL); // 回滚
        return -EIO;
    }

    return 0;
}

static void __exit my_module_exit(void)
{
    // 资源清理工作
}

module_init(my_module_init);
module_exit(my_module_exit);
MODULE_LICENSE("GPL");