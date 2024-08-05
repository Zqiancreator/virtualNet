/*******************************************************************************
                              Include header files
 ******************************************************************************/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_tun.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/pci.h>
#include "libxdma.h"
#include "xdma_mod.h"
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/page.h>
#include "zringbuffer.h"
#include <linux/cpumask.h>
// #include "znotify.h"
// #include "xdma_thread.h"
// #include "libxdma.c"
#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
                              Macro definitions
 ******************************************************************************/
#define MHYTUN_DEV_NAME "veth_net0"
#define MHYTUN_IP_ADDR "192.168.1.2"
#define MHYTUN_NETMASK "255.255.255.0"
#define MHYTUN_MAC_ADDR {0x02, 0x11, 0x22, 0x33, 0x44, 0x55}
#define FPGATUN_MAC_ADDR {0x02, 0x11, 0x22, 0x33, 0x44, 0x56}
#define ONE_GB 0x40000000
#define PCIE_DEV_H2C "/dev/xdma0_h2c_0"
#define PCIE_DEV_C2H "/dev/xdma0_c2h_0"
#define H2C_OFFSET 0x800000000
#define C2H_OFFSET 0x820000000
#define RINGBUFFER_SIZE ((1 << RING_BUFF_DEPTH) * PACK_SIZE)
#define MAX_SKBUFFS 16
#define DEST_HOST_IP 0x0c18a8c0
#define DEST_CARD_IP 0x0d18a8c0
#define BOARD_IP 0xff18a8c0
#define MTU_SIZE 2000
#define PACK_SIZE MAX_SKBUFFS *MTU_SIZE // TODO
#define PCI_VID 0x10EE
#define PCI_PID 0x9034
#define SGL_NUM 2
#define TIME_OUT 5
#define MSI_TIME_OUT 6000
#define MS_TO_JIFFIES(ms) ((ms) * HZ / 1000)
#define RING_BUFF_DEPTH 6
#define MSI_BIT 0x04
    /*******************************************************************************
                                  Type definitions
     ******************************************************************************/
    unsigned char fpga_mac_src[] = FPGATUN_MAC_ADDR;
    unsigned char mhy_mac_src[] = MHYTUN_MAC_ADDR;
    static struct packet_type mytun_packet_type;
    static struct packet_type pci_packet_type;
    // static int  receiveTimes;
    int skb_count[SGL_NUM]; // skb num now
    struct sk_buff *pstskb_array[SGL_NUM][MAX_SKBUFFS];
    struct scatterlist *sgl[SGL_NUM];
    struct sg_table sgt;
    int sgl_current;
    struct pci_dev *pdev;
    static struct task_struct *send_thread;
    static struct task_struct *receive_thread;
    static struct task_struct *intr_thread;
    static struct timer_list my_timer;
    static struct timer_list msi_timer;
    static unsigned long timer_interval_ms = TIME_OUT; // 定时器间隔（毫秒）
    static unsigned long msi_interval_ms = MSI_TIME_OUT; // 定时器间隔（毫秒）
    struct xdma_cdev *xcdev;
    struct xdma_dev *xdev;
    // wait queue
    static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
    static int read_condition = 0;
    static int Intr_condition = 0;
    static int write_condition = 0;
    unsigned char *intrBuf; // TODO 可以在线程里面定义
    unsigned char *IntrAck;
    unsigned char *msiData;
    unsigned char *update_magic_num;
    loff_t intrPos = 0x82000040;
    loff_t msiTest = 0x82000078;
    loff_t msiCnt = 0x820000d0;
    loff_t msiClear = 0x82000070;
    loff_t msiReq = 0x82000068;
    loff_t updateCnt = 0x82000054;
    loff_t updateAck = 0x82000044;
    struct net_device *mydev;
    /*******************************************************************************
                                  Local function declarations
     ******************************************************************************/
    int pci_send(struct xdma_cdev *xcdev, struct xdma_dev *xdev, loff_t offst);
    int pcie_init(void);
    int pcie_open(void);
    void pcie_close(void);
    static int Send_thread(void *data);
    static int Receive_thread(void *data);
    static int Intr_thread(void *data);
    static void Timer_Callback(struct timer_list *timer);
    static void Msi_Timer(struct timer_list *timer);
    static int configSKB(struct sk_buff *ddr_skb, unsigned char *skb_data, bool afterSetData);
    static void syncRingbuffer(void);
    /*******************************************************************************
                                  extern function
     ******************************************************************************/
    extern ssize_t my_xdma_xfer_submit(void *dev_hndl, int channel, u64 ep_addr,
                                       struct sg_table *sgt, int timeout_ms);
    extern ssize_t xdma_xfer_submit(void *dev_hndl, int channel, bool write, u64 ep_addr,
                                    struct sg_table *sgt, bool dma_mapped, int timeout_ms);
    /*******************************************************************************
                                  Global function declarations
     ******************************************************************************/
    static struct net_device *g_stmytundev;

    static struct ifreq stifrtest;
    typedef struct
    {
        char *h2c0_path;
        char *buffer_h2c;
        u_int64_t buf_h2c_size;
        struct file *h2c0;

        char *c2h0_path;
        char *buffer_c2h;
        u_int64_t buf_c2h_size;
        struct file *c2h0;
    } xdma_device;
    /* Assuming PCIe device */
    xdma_device g_stpcidev;
    /*******************************************************************************
                           Inline function implementations
    *******************************************************************************/

    extern int register_my_notifier(struct notifier_block *nb);
    extern int unregister_my_notifier(struct notifier_block *nb);
    // 通知处理函数
    static int my_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
    { // 等待中断处理函数通知,然后唤醒读取ddr的线程
        // printk(KERN_ERR "debug: Notification received!, data:%s\n",(char*)data);
        if(Intr_condition == 0) {
            Intr_condition = 1;
            wake_up_interruptible(&my_wait_queue);
        }
        else
            printk(KERN_ERR "error: Intr_condition is not 0\n");
        return NOTIFY_OK;
    }
    // 定义一个通知块
    static struct notifier_block my_notifier_block = {
        .notifier_call = my_notifier_call,
    };
    /*******************************************************************************
                           Local function implementations
    *******************************************************************************/
    static int mytun_open(struct net_device *dev)
    {
        // printk(KERN_ERR "open net dev\n");
        netif_start_queue(dev);
        return 0;
    }

    static int mytun_stop(struct net_device *dev)
    {
        // printk(KERN_ERR "stop net dev\n");
        netif_stop_queue(dev);
        return 0;
    }

    static netdev_tx_t mytun_start_xmit(struct sk_buff *skb, struct net_device *dev)
    {
        // 收到包后重置定时器，数量达到MAX则调用Callback，和超时处理一样
        // Transmit packet logic here
        struct iphdr *iph_xmit = (struct iphdr *)(skb_network_header(skb));
        if (iph_xmit->saddr == 0 && iph_xmit->daddr != 0xd18a8c0 && iph_xmit->daddr != 0xc18a8c0)
        {
            // printk(KERN_ERR "start xmit: error saddr=%x, daddr=%x\n",iph_xmit->saddr, iph_xmit->daddr);
            return NETDEV_TX_OK;
        }

        // printk(KERN_ERR "debug: iph_xmit->daddr:0x%x, iph_xmit->saddr:0x%x,skb->len:%x,skb->protocol:%x\n",iph_xmit->daddr,iph_xmit->saddr,skb->len,skb->protocol);

        if (skb_count[sgl_current] < MAX_SKBUFFS)
        {
            if(skb_count[sgl_current] < 0)
                printk(KERN_ERR "error start_xmit skb_count[sgl_current] = %d\n", skb_count[sgl_current]);

            pstskb_array[sgl_current][skb_count[sgl_current]++] = skb;
            // printk(KERN_ERR "skb count:%d\n",skb_count[sgl_current]);
            if (skb_count[sgl_current] == MAX_SKBUFFS){
                printk(KERN_ERR "skb count is full\n");
                mod_timer(&my_timer, jiffies - 1); // 立即超时，自动调用回调函数
            }
            else
                mod_timer(&my_timer, jiffies + MS_TO_JIFFIES(timer_interval_ms));

        }
        else
        {
            kfree_skb(skb); // Drop the packet if the array is full
        }
        // dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    static int mytun_set_mac_address(struct net_device *dev, void *p)
    {
        struct sockaddr *addr = p;

        // printk(KERN_INFO "Setting MAC address to %pM\n", addr->sa_data); // 举例输出
        return eth_mac_addr(dev, p); // 或根据实际情况实现自定义逻辑
    }

    static const struct net_device_ops mytun_netdev_ops = {
        .ndo_open = mytun_open,
        .ndo_stop = mytun_stop,
        .ndo_start_xmit = mytun_start_xmit,
        .ndo_set_mac_address = mytun_set_mac_address,
        // Add other operations as needed
    };

    static void syncRingbuffer(){
        int ret;
        char *updateSync;
        updateSync = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        updateSync[0] = 0xFF;
        updateSync[1] = 0xFF;
        updateSync[2] = 0xFF;
        updateSync[3] = 0xFF;
        ret = kernel_write(g_stpcidev.h2c0, updateSync, 4, &updateCnt); // sync msi count 
        printk(KERN_ERR "send sync intr\n");
        ret = kernel_write(g_stpcidev.h2c0, intrBuf, 4, &intrPos); // send inter
        while(updateSync[0] != 0x00){
            ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0, updateSync, 4, &updateCnt); 
        }
        updateSync[0] = 0x00;
        updateSync[1] = 0x00;
        updateSync[2] = 0x00;
        updateSync[3] = 0x00;
        ret = kernel_write(g_stpcidev.h2c0, updateSync, 4, &updateCnt); // clear msi count 
        printk(KERN_ERR "sync ringbuffer success\n");
    }

    static int mytun_rx_handler(
        struct sk_buff *pstskb,
        struct net_device *pstdev,
        struct packet_type *pstpt,
        struct net_device *pstorigdev)
    { // 接收到数据包直接发回上层
        return NET_RX_SUCCESS;
    }

    static int Send_thread(void *data)
    { // wait for condition and send data
        int ret;
        loff_t offst = H2C_OFFSET;
        int pack_offset;
        intrBuf = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        IntrAck = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        update_magic_num = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        // 将 0xFFFF 存储到缓冲区中
        intrBuf[0] = 0xFF;
        intrBuf[1] = 0xFF;
        intrBuf[2] = 0xFF;
        intrBuf[3] = 0xFF;

        update_magic_num[0] = 0x69;
        update_magic_num[1] = 0x69;
        update_magic_num[2] = 0x69;
        update_magic_num[3] = 0x69;
        while (1)
        {
            wait_event_interruptible(my_wait_queue, write_condition);
            if (skb_count[sgl_current] == 0)
            {
                printk(KERN_ERR "error send_thread: skb_count[sgl_current]=0,skb_count[1-sgl_current]=%x\n", skb_count[1-sgl_current]);
                continue;
            }
            // printk(KERN_ERR "send thread ready to send skb count:%d\n",skb_count[sgl_current]);
            sgl_current = 1 - sgl_current; // 设置current改变，现在就可以开始接收

            pci_send(xcdev, xdev, offst);

            printk(KERN_ERR "send end, offst=%x\n",offst);
            offst += PACK_SIZE;
            if (offst >= (RINGBUFFER_SIZE + H2C_OFFSET))
            {
                offst = H2C_OFFSET;
            }

            while (--skb_count[1 - sgl_current] >= 0)
            {
                kfree_skb(pstskb_array[1 - sgl_current][skb_count[1 - sgl_current]]); // TODO 是不是不用释放
            }

            ret = kernel_write(g_stpcidev.h2c0, update_magic_num, 4, &updateCnt); // increase cnt
            ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0, IntrAck, 4, &updateAck);
            if ((IntrAck[0] >> 4) == 0x0)
            {   /* 中断已经被清除,发送新的中断 */
                ret = kernel_write(g_stpcidev.h2c0, intrBuf, 4, &intrPos); 
                printk(KERN_ERR "send inter\n");
            }
            
            ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0, IntrAck, 4, &updateCnt);
            printk(KERN_ERR "update cnt=%x,%x,%x,%x\n", IntrAck[0], IntrAck[1], IntrAck[2], IntrAck[3]);

            skb_count[1 - sgl_current] = 0;
            write_condition = 0;
        }
        // never reach
        return 0;
    }

    static int Intr_thread(void *data)
    { // 接收中断线程 移动ringbuffer的Write指针，如果ringbuffer为空则通知读ddr线程
        int ret;
        u_int16_t preIndex = 0;
        u_int16_t currentIndex = 0;
        u_int16_t readMsi = 0;
        unsigned char *msiIndex;
        msiData = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        msiIndex = kmalloc(sizeof(int) * 4, GFP_KERNEL);
        msiData[0] = MSI_BIT;
        msiData[1] = 0x00;
        msiData[2] = 0x00;
        msiData[3] = 0x00;
        while (1)
        { // 等待被唤醒，读取中断寄存器，改变ringbuffer指针,若ringbuffer为空，通知read
            wait_event_interruptible(my_wait_queue, Intr_condition);
            ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0, msiIndex, 4, &msiCnt);
            if (msiIndex[3] == 0x80)
            {
                // todo 同步
                printk(KERN_ERR "begin sync\n");
                msiIndex[0] = 0x00;
                msiIndex[1] = 0x00;
                msiIndex[2] = 0x00;
                ret = kernel_write(g_stpcidev.h2c0, msiIndex, 3, &msiCnt); // clear msi count 
                ringbuffer->bRdIx = 0;
                ringbuffer->bWrIx = 0;
            }
            else
            {
                readMsi = (msiIndex[1] << 8) | msiIndex[0];
                printk(KERN_ERR "readMsi=%x\n", readMsi);
                // printk(KERN_ERR "msiIndex=%x,%x,%x,%x,readMsi=%x, preIndex=%x\n", msiIndex[0], msiIndex[1], msiIndex[2], msiIndex[3], readMsi, preIndex);
                if (readMsi >= preIndex)
                    currentIndex = readMsi - preIndex;
                else
                    currentIndex = readMsi + (0xFFFF - preIndex + 1);

                preIndex = readMsi;
                ringbuffer->bWrIx += currentIndex;
                ringbuffer->bWrIx &= ringbuffer->bMax;
                printk(KERN_ERR "debug Intr_thread: ringbuffer->bWrIx:%x,ringbuffer->bRdIx:%x, currentIndex = %x\n", ringbuffer->bWrIx, ringbuffer->bRdIx, currentIndex);
                if (((ringbuffer->bRdIx + currentIndex) & ringbuffer->bMax) == ringbuffer->bWrIx)
                { // TODO
                    if(read_condition == 0) {
                        printk(KERN_ERR "debug Intr_thread: ringbuffer is empty wake up read\n");
                        read_condition = 1;
                    } else {
                        printk(KERN_ERR "error: read_condition is not 0\n");
                    }
                    wake_up_interruptible(&my_wait_queue);
                }
            }
            ret = kernel_write(g_stpcidev.h2c0, msiData, 4, &msiTest); // clear msi inter
            Intr_condition = 0;
            mod_timer(&msi_timer, jiffies + MS_TO_JIFFIES(msi_interval_ms));
            printk(KERN_ERR "clear msi inter\n");
        }
        return 0;
    }

    static int Receive_thread(void *data)
    { // 读取ddr线程 从ddr读取数据后调用rx-handler发送回上层IP层
        unsigned char *skb_data;
        struct sk_buff *ddr_skb;
        struct iphdr *iph;
        ssize_t ret;
        int index = 0;
        loff_t offst = C2H_OFFSET;
        mydev = dev_get_by_name(&init_net, MHYTUN_DEV_NAME);
        if (!mydev)
        {
            pr_err("Failed to get network device\n");
            return -ENODEV;
        }

        while (1)
        {
            wait_event_interruptible(my_wait_queue, read_condition);
            read_condition = 0;
            while (ringbuffer->bRdIx != ringbuffer->bWrIx)
            {
                while (index++ < MAX_SKBUFFS)
                {
                    ddr_skb = alloc_skb(MTU_SIZE + NET_IP_ALIGN, GFP_KERNEL);
                    if (!ddr_skb)
                    {
                        pr_err("error Failed to allocate ddr_skb\n");
                        return -ENOMEM;
                    }
                    skb_reserve(ddr_skb, NET_IP_ALIGN);
                    skb_data = skb_put(ddr_skb, MTU_SIZE);
                    ddr_skb->dev = mydev;
                    // configSKB(ddr_skb, skb_data, false);
                    // printk(KERN_ERR "read offset=%x\n",offst);
                    ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0, skb_data, MTU_SIZE, &offst);
                    if (ret <= 0) {
                        printk(KERN_ERR "Rx-thread read ddr error,ret=%x\n", ret);
                        index--;
                        break;
                    }

                    // configSKB(ddr_skb, skb_data, true);
                    skb_set_network_header(ddr_skb, sizeof(struct ethhdr)); // reset or set
                    skb_set_transport_header(ddr_skb, sizeof(struct ethhdr) + sizeof(struct iphdr));

                    ddr_skb->protocol = eth_type_trans(ddr_skb, mydev); // 设置协议为IP
                    ddr_skb->ip_summed = CHECKSUM_UNNECESSARY;
                    if (ddr_skb)
                    {
                        // printk(KERN_ERR "debug Rx-thread ringbuffer->bRead:%x,ringbuffer->bWrIx:%x,offst=%llx,index=%d\n",ringbuffer->bRdIx,ringbuffer->bWrIx,offst,index);

                        if (netif_rx(ddr_skb) == NET_RX_SUCCESS)
                        {
                            if (ddr_skb->data[1] == 0xff || ddr_skb->data[1] == 0x00)
                            {
                                printk(KERN_ERR "data is null index=%d, offset=%llx\n",index, (offst - index * MTU_SIZE) + MTU_SIZE);
                                index--;
                                break;
                            }
#if 0
                            iph = (struct iphdr *)(skb_network_header(ddr_skb));
                            if(iph)
                                printk(KERN_ERR "Receive-thread Protocol:%d, len:%d, ipsaddr:%x, daddr:%x\n",iph->protocol, ddr_skb->len, iph->saddr, iph->daddr); 
                            else
                                printk(KERN_ERR "debug Receive-thread iph is null");
#endif
                            // printk(KERN_ERR "debug2 Rx-thread ready send to ip: iph->saddr=%x,iph->daddr=%x,iph->protocol=%x,skb->protocol=%x\n",iph->saddr,iph->daddr,iph->protocol,ddr_skb->protocol);
                            // printk(KERN_ERR "netif_rx run success\n");
                        }
                        else
                        {
                            kfree_skb(ddr_skb);
                            printk(KERN_ERR "Rx-thread netif_rx run error\n");
                        }
                    }
                    else
                        printk(KERN_ERR "Rx-thread ddr_skb is null");


                    offst += MTU_SIZE;
                    if (offst >= (RINGBUFFER_SIZE + C2H_OFFSET))
                    {
                        offst = C2H_OFFSET;
                    }
                }

                while (index++ < MAX_SKBUFFS)
                {
                    offst += MTU_SIZE;
                    if (offst >= (RINGBUFFER_SIZE + C2H_OFFSET))
                    {
                        offst = C2H_OFFSET;
                    }
                }
                index = 0;
                ringbuffer->bRdIx += 1;
                ringbuffer->bRdIx &= ringbuffer->bMax;
            }
        }
        // never reach
        return 0;
    }

    static int configSKB(struct sk_buff *ddr_skb, unsigned char *skb_data, bool afterSetData)
    {
        if (afterSetData)
        {
            ddr_skb->dev = mydev;
            ddr_skb->protocol = eth_type_trans(ddr_skb, mydev); // 设置协议为IP
            ddr_skb->ip_summed = CHECKSUM_UNNECESSARY;
            skb_set_network_header(ddr_skb, sizeof(struct ethhdr)); // reset or set
            skb_set_transport_header(ddr_skb, sizeof(struct ethhdr) + sizeof(struct iphdr));
        }
        else
        {
            ddr_skb = alloc_skb(MTU_SIZE + NET_IP_ALIGN, GFP_KERNEL);
            if (!ddr_skb)
            {
                pr_err("Failed to allocate ddr_skb\n");
                return -ENOMEM;
            }
            skb_reserve(ddr_skb, NET_IP_ALIGN);
            skb_data = skb_put(ddr_skb, MTU_SIZE);
        }
        return 0;
    }

    static void Msi_Timer(struct timer_list *timer){
        if(Intr_condition == 0) {
            printk(KERN_ERR "msi timer wake up Intr_thread\n");
            Intr_condition = 1;
            wake_up_interruptible(&my_wait_queue);
        }
        else
            printk(KERN_ERR "error: msi timer Intr_condition is not 0\n");
        return NOTIFY_OK;
    }

    static void Timer_Callback(struct timer_list *timer)
    { // wake up send_thread
        if (skb_count[sgl_current] == 0){
            printk(KERN_ERR "error skb_count is 0, skb_count[1-sgl_current] = %d\n", skb_count[1-sgl_current]);
            return;
        }
        // printk(KERN_ERR "ready wake skb_count[sgl_current]:%d\n",skb_count[sgl_current]);
        if (skb_count[1 - sgl_current] == 0&&write_condition == 0)
        { // 上一个数据包已经发送完成 TODO
            printk(KERN_ERR "mytimer Call back skb_count = %d\n", skb_count[sgl_current]);
            write_condition = 1;
            wake_up_interruptible(&my_wait_queue);
        }
        else
        {
            printk(KERN_ERR "error last pack is sending skb_count[1-sgl_current]=%d, skb_count[sgl_current] = %d, write_condition=%d\n", skb_count[1 - sgl_current], skb_count[sgl_current], write_condition);
        }
        return;
    }

    int pci_send(struct xdma_cdev *xcdev, struct xdma_dev *xdev, loff_t offst)
    {

        int nents, i = 0;
        sg_init_table(sgl[1 - sgl_current], skb_count[1 - sgl_current]);

        for (i = 0; i < skb_count[1 - sgl_current]; i++)
        {
            sg_set_buf(&sgl[1 - sgl_current][i], pstskb_array[1 - sgl_current][i]->data, MTU_SIZE); // pstskb_array[1-sgl_current][i]->len
        }
        printk(KERN_ERR "before, %x\n", skb_count[1 - sgl_current]);
        nents = pci_map_sg(pdev, sgl[1 - sgl_current], skb_count[1 - sgl_current], DMA_TO_DEVICE);
        if (nents == 0)
        {
            printk(KERN_ERR "pci_send Failed to map scatterlist\n");
            return 1;
        }
        printk(KERN_ERR "after\n");
        // 设置 sg_table
        sgt.sgl = sgl[1 - sgl_current];
        sgt.nents = nents;
        sgt.orig_nents = skb_count[1 - sgl_current];
        my_xdma_xfer_submit(xdev, 0, offst, &sgt, 0);

        pci_unmap_sg(pdev, sgt.sgl, sgt.orig_nents, DMA_TO_DEVICE);
        // printk(KERN_ERR "pci_send: xdma_xfer_submit, offst=%llx\n",offst);
        // xdma_xfer_submit(xdev,0,1,offst,&sgt,1,10000);
        return 0;
    }

    int pcie_init(void)
    {
        int status = 0;

        g_stpcidev.h2c0_path = PCIE_DEV_H2C;
        g_stpcidev.c2h0_path = PCIE_DEV_C2H;

        return status;
    }

    // 打开PCIe设备
    int pcie_open(void)
    {
        int status = 0;
        // mm_segment_t old_fs;

        // old_fs = get_fs();
        // set_fs(KERNEL_DS);

        /* 打开 XDMA Host-to-Card 0 设备 */
        g_stpcidev.h2c0 = filp_open(g_stpcidev.h2c0_path, O_RDWR, S_IRUSR | S_IWUSR);
        if (IS_ERR(g_stpcidev.h2c0))
        {
            pr_err("FAILURE: Could not open %s. Make sure g_stpcidev device driver is loaded and you have access rights.\n", g_stpcidev.h2c0_path);
            status = PTR_ERR(g_stpcidev.h2c0);
            goto cleanup_handles;
        }

        /* 打开 XDMA Card-to-Host 0 设备 */
        g_stpcidev.c2h0 = filp_open(g_stpcidev.c2h0_path, O_RDWR, S_IRUSR | S_IWUSR);
        if (IS_ERR(g_stpcidev.c2h0))
        {
            pr_err("FAILURE: Could not open %s.\n", g_stpcidev.c2h0_path);
            status = PTR_ERR(g_stpcidev.c2h0);
            goto cleanup_handles;
        }
        g_stpcidev.h2c0->f_pos = H2C_OFFSET;
        g_stpcidev.c2h0->f_pos = C2H_OFFSET;
        /* 恢复原始的地址空间 */
        // set_fs(old_fs);

        return status;

    cleanup_handles:
        if (!IS_ERR(g_stpcidev.c2h0) && g_stpcidev.c2h0)
            filp_close(g_stpcidev.c2h0, NULL);
        if (!IS_ERR(g_stpcidev.h2c0) && g_stpcidev.h2c0)
            filp_close(g_stpcidev.h2c0, NULL);
        // set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
        return status;
    }

    // 关闭PCIe设备
    void pcie_close(void)
    {
        // mm_segment_t old_fs;

        // old_fs = get_fs();
        // set_fs(KERNEL_DS);
        if (g_stpcidev.c2h0)
            filp_close(g_stpcidev.c2h0, NULL);
        if (g_stpcidev.h2c0)
            filp_close(g_stpcidev.h2c0, NULL);
        if (g_stpcidev.buffer_c2h)
            kfree(g_stpcidev.buffer_c2h);
        if (g_stpcidev.buffer_h2c)
            kfree(g_stpcidev.buffer_h2c);
        // set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
    }

    static int __init mytun_init(void)
    {
        int err;
        err = register_my_notifier(&my_notifier_block);
        if (err)
        {
            printk(KERN_ERR "Failed to register notifier: %d\n", err);
        }
        ringbuffer = Cl2FifoCreateFifo(RING_BUFF_DEPTH);
        // printk(KERN_ERR "module_init\n" );
        g_stmytundev = alloc_netdev(0, MHYTUN_DEV_NAME, NET_NAME_ENUM, ether_setup);
        if (!g_stmytundev)
        {
            printk(KERN_ERR "alloc_error\n");
            return -ENOMEM;
        }

        g_stmytundev->netdev_ops = &mytun_netdev_ops;
        g_stmytundev->flags |= IFF_NOARP;
        g_stmytundev->features |= NETIF_F_IP_CSUM; // NETIF_F_NO_CSUM;
        memcpy(g_stmytundev->dev_addr, mhy_mac_src, ETH_ALEN);

        strcpy(stifrtest.ifr_name, MHYTUN_DEV_NAME);
        stifrtest.ifr_flags = IFF_UP;
        memcpy(stifrtest.ifr_hwaddr.sa_data, mhy_mac_src, ETH_ALEN);
        stifrtest.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        // printk(KERN_ERR "sa_family: %d\n", stifrtest.ifr_hwaddr.sa_family);

        err = register_netdev(g_stmytundev);
        if (err)
        {
            printk(KERN_ERR "register error,ret=%x\n", err);
            return err;
        }

        rtnl_lock();
        if (dev_change_flags(g_stmytundev, IFF_UP, NULL) < 0)
        {
            free_netdev(g_stmytundev);
            return err;
        }

        if (netif_running(g_stmytundev))
        {
            netif_stop_queue(g_stmytundev);
            dev_close(g_stmytundev);
        }

        err = dev_set_mac_address(g_stmytundev, &stifrtest.ifr_hwaddr, NULL);
        if (err < 0)
        {
            printk(KERN_ERR "set mac addr error err:%d\n", err);
            // free_netdev(g_stmytundev);
            return err;
        }
        dev_open(g_stmytundev, NULL);
        netif_start_queue(g_stmytundev);
        rtnl_unlock();

        /* Set IP address and netmask */
        struct in_device *in_dev = __in_dev_get_rtnl(g_stmytundev);
        if (!in_dev)
        {
            printk(KERN_ERR "error init Failed to get in_device\n");
            unregister_netdev(g_stmytundev);
            free_netdev(g_stmytundev);
            return err;
        }
// 分配新的接口地址结构
#if 0
    struct in_ifaddr *ifa;
    ifa = kzalloc(sizeof(*ifa), GFP_KERNEL);
    if (!ifa) {
        err = -ENOMEM;
        return err;
    }
    // 设置接口地址结构
    ifa->ifa_prefixlen = 24; //  255.255.255.0 的掩码
    ifa->ifa_address = htonl(in_aton(MHYTUN_IP_ADDR)); //  IP 地址
    ifa->ifa_local = ifa->ifa_address;
    ifa->ifa_broadcast = htonl(in_aton(MHYTUN_IP_ADDR)); // 广播地址
    ifa->ifa_mask = htonl(in_aton(MHYTUN_NETMASK)); // 子网掩码

    printk(KERN_ERR "before add ifa_list\n");
    // 加入到接口地址列表
    if(in_dev->ifa_list == NULL){
        printk(KERN_ERR " ifa_list is null\n");
        ifa->ifa_next = NULL;
    }
    else {
        printk(KERN_ERR " ifa_list is not null\n");
        ifa->ifa_next = in_dev->ifa_list;
    }
#endif

        /* Set up packet handler for PCIe device */
        err = pcie_init();
        err = pcie_open();
        if (err != 0)
        {
            printk(KERN_ERR "Failed to find PCIe device\n");
            unregister_netdev(g_stmytundev);
            free_netdev(g_stmytundev);
            return err;
        }

        /* 初始化 packet_type 结构体 */
        memset(&mytun_packet_type, 0, sizeof(struct packet_type));
        mytun_packet_type.type = htons(ETH_P_ALL); // 只接收IP数据包 ETH_P_ALL//接收所有数据包
        mytun_packet_type.func = mytun_rx_handler;
        mytun_packet_type.dev = NULL; // NULL 表示接收所有设备的包
                                      // sgl alloc
        int sgli = 0;
        for (; sgli < SGL_NUM; sgli++)
        {
            sgl[sgli] = kzalloc(sizeof(struct scatterlist) * MAX_SKBUFFS, GFP_KERNEL);
            skb_count[sgli] = 0;
        }
        // init
        sgl_current = 0;
        pdev = pci_get_device(PCI_VID, PCI_PID, NULL);
        // sgl end

        // rx-handler
        xcdev = (struct xdma_cdev *)g_stpcidev.h2c0->private_data;
        xdev = xcdev->xdev;
        timer_setup(&my_timer, Timer_Callback, 0);
        timer_setup(&msi_timer, Msi_Timer, 0);
        send_thread = kthread_run(Send_thread, NULL, "send_thread");
        if (IS_ERR(send_thread))
        {
            printk(KERN_ERR "Failed to create thread1\n");
            return PTR_ERR(send_thread);
        }
        receive_thread = kthread_run(Receive_thread, NULL, "receive_thread");
        if (IS_ERR(receive_thread))
        {
            printk(KERN_ERR "Failed to create thread2\n");
            return PTR_ERR(receive_thread);
        }
        intr_thread = kthread_run(Intr_thread, NULL, "intr_thread");
        if (IS_ERR(intr_thread))
        {
            printk(KERN_ERR "Failed to create thread3\n");
            return PTR_ERR(intr_thread);
        }

#if 1
        cpumask_t cpuset;

        // set every thread to different cpu
        cpumask_clear(&cpuset);
        cpumask_set_cpu(0, &cpuset); // Read_thread on CPU 0
        set_cpus_allowed_ptr(send_thread, &cpuset);

        cpumask_clear(&cpuset);
        cpumask_set_cpu(1, &cpuset);
        set_cpus_allowed_ptr(receive_thread, &cpuset);

        cpumask_clear(&cpuset);
        cpumask_set_cpu(2, &cpuset);
        set_cpus_allowed_ptr(intr_thread, &cpuset);
#endif

        dev_add_pack(&mytun_packet_type);

        return 0;
    }

    static void __exit mytun_exit(void)
    {
        dev_remove_pack(&mytun_packet_type);
        dev_remove_pack(&pci_packet_type);
        pcie_close();
        // sgl free
        int sgli = 0;
        for (; sgli < SGL_NUM; sgli++)
        {
            kfree(sgl[sgli]);
        }

        // dev_put(g_stpcidev);
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        del_timer(&my_timer);
        del_timer(&msi_timer);
        if (send_thread)
        {
            kthread_stop(send_thread);
        }

        if (receive_thread)
        {
            kthread_stop(receive_thread);
        }

        if (intr_thread)
        {
            kthread_stop(intr_thread);
        }

        Cl2FifoRemoveFifo(ringbuffer);
        printk(KERN_INFO "TUN device removed: %s\n", MHYTUN_DEV_NAME);
    }

    module_init(mytun_init);
    module_exit(mytun_exit);

    MODULE_LICENSE("GPL");
    MODULE_AUTHOR("Your Name");
#ifdef __cplusplus
}
#endif
