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
extern "C" {
#endif

/*******************************************************************************
                              Macro definitions                               
 ******************************************************************************/
#define MHYTUN_DEV_NAME 		"veth_net0"
#define MHYTUN_IP_ADDR 			"192.168.1.2"
#define MHYTUN_NETMASK 			"255.255.255.0"
#define MHYTUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x55}
#define FPGATUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x56}
#define ONE_GB                  0x40000000
#define PCIE_DEV_H2C            "/dev/xdma0_h2c_0"
#define PCIE_DEV_C2H            "/dev/xdma0_c2h_0"
#define H2C_OFFSET              0x800000000
#define C2H_OFFSET              0x820000000
#define RINGBUFFER_SIZE         ((1<<RING_BUFF_DEPTH)*PACK_SIZE)
#define MAX_SKBUFFS             2 
#define DEST_HOST_IP            0x0c18a8c0
#define DEST_CARD_IP            0x0d18a8c0
#define BOARD_IP                0xff18a8c0
#define MTU_SIZE                1496
#define PACK_SIZE               MAX_SKBUFFS*MTU_SIZE // TODO
#define PCI_VID                 0x10EE
#define PCI_PID                 0x9034
#define SGL_NUM                 2
#define TIME_OUT                0
#define MS_TO_JIFFIES(ms)       ((ms) * HZ / 1000)
#define RING_BUFF_DEPTH         6
#define WriteRingSize           100
#define FREE_SKB_MAX            100
/*******************************************************************************
                              Type definitions                                
 ******************************************************************************/
unsigned char fpga_mac_src[] = FPGATUN_MAC_ADDR;
unsigned char mhy_mac_src[] = MHYTUN_MAC_ADDR;
static struct packet_type mytun_packet_type;
static struct packet_type pci_packet_type;
// static int  receiveTimes;
int skb_count[SGL_NUM];      // skb num now
struct sk_buff 					*pstskb_array[SGL_NUM][MAX_SKBUFFS];
struct scatterlist *sgl[SGL_NUM];


struct sg_table sgt;
int sgl_current;    
struct pci_dev *pdev;
static struct task_struct *send_thread;
static struct task_struct *receive_thread;
static struct task_struct *intr_thread;
static struct timer_list my_timer;
static unsigned long timer_interval_ms = TIME_OUT; // 定时器间隔（毫秒）
struct xdma_cdev *xcdev; 
struct xdma_dev *xdev;
// wait queue
static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int read_condition = 0;
static int Intr_condition = 0;
static int write_condition = 0;
char *intrBuf; // TODO 可以在线程里面定义
char *clearIntr;
char *msiData;
loff_t intrPos=0x82000040;
loff_t msiReq= 0x82000078;
struct net_device * mydev;
static bool start_xmit_first = true;

#if 1 /*计时每阶段耗时*/
    ktime_t start, end;
    ktime_t start2, end2;
    ktime_t start3, end3;
    ktime_t start4, end4;
    ktime_t start5, end5;
    ktime_t start6, end6;
    s64 delta;
    s64 delta2;
    s64 delta3;
    s64 delta4;
    s64 delta5;
    s64 delta6;
#endif

/*******************************************************************************
                         	  Local function declarations                          
 ******************************************************************************/
int pci_send(struct xdma_cdev *xcdev, struct xdma_dev *xdev, loff_t offst);
int pcie_init(void);
int pcie_open(void);
void pcie_close(void);
static int Send_thread(void* data);
static int Receive_thread(void* data);
static int Intr_thread(void* data);
static void Timer_Callback(struct timer_list *timer);
static int configSKB(struct sk_buff *ddr_skb, unsigned char* skb_data, bool afterSetData);

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
static struct net_device 		*g_stmytundev;

static struct ifreq 		stifrtest;
typedef struct{
    char *h2c0_path;
    char *buffer_h2c;
    u_int64_t buf_h2c_size;
    struct file* h2c0;

    char *c2h0_path;
    char *buffer_c2h;
    u_int64_t buf_c2h_size;
    struct file* c2h0;
} xdma_device;
/* Assuming PCIe device */
xdma_device g_stpcidev;

typedef struct skb_node {
    struct sk_buff *skb;
    struct skb_node *next;
}skb_node;

// 链表头结构
typedef struct skb_list {
    int count;
    struct skb_node *head;
}skb_list;

skb_list *skb_head;
typedef struct Store_skb_list 
{
    skb_list *ringbuffer[FREE_SKB_MAX];
    u_int16_t Free;
    u_int16_t Used;
    u_int16_t count;
}FreeSkbList;

typedef struct WriteRingbuffer
{
    /* data */
    skb_list *ringbuffer[WriteRingSize];
    u_int16_t WrInx;
    u_int16_t RdInx;
}WriteRingbuffer;
WriteRingbuffer* writeRingbuffer;
FreeSkbList* freeSkbList;
/*******************************************************************************
                       Inline function implementations                        
*******************************************************************************/

extern int register_my_notifier(struct notifier_block *nb);
extern int unregister_my_notifier(struct notifier_block *nb);
// 通知处理函数
static int my_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
{// 等待中断处理函数通知,然后唤醒读取ddr的线程
    // printk(KERN_ERR "debug: Notification received!, data:%s\n",(char*)data);
    
    Intr_condition = 1;
    wake_up_interruptible(&my_wait_queue);
    return NOTIFY_OK;
}
// 定义一个通知块
static struct notifier_block my_notifier_block = {
    .notifier_call = my_notifier_call,
};
/*******************************************************************************
                       Local function implementations                         
*******************************************************************************/
static int mytun_open(struct net_device *dev) {
    // printk(KERN_ERR "open net dev\n");
    netif_start_queue(dev);
    return 0;
}

static int mytun_stop(struct net_device *dev) {
    // printk(KERN_ERR "stop net dev\n");
    netif_stop_queue(dev);
    return 0;
}

static netdev_tx_t mytun_start_xmit(struct sk_buff *skb, struct net_device *dev) {
    // 收到包后重置定时器，数量达到MAX则调用Callback，和超时处理一样
    // Transmit packet logic here
    struct iphdr *iph_xmit = (struct iphdr *)(skb_network_header(skb));
    skb_node * tmp;
    static bool UseFree = false;
    if(iph_xmit->saddr==0&&iph_xmit->daddr!=0xd18a8c0&&iph_xmit->daddr!=0xc18a8c0){
        // printk(KERN_ERR "start xmit: error saddr=%x, daddr=%x\n",iph_xmit->saddr, iph_xmit->daddr);
        return NETDEV_TX_OK;
    }

    // printk(KERN_ERR "debug: iph_xmit->daddr:0x%x, iph_xmit->saddr:0x%x,skb->len:%x,skb->protocol:%x\n",iph_xmit->daddr,iph_xmit->saddr,skb->len,skb->protocol);

    #if 0 /*debug 打印具体数据*/
        static bool flag = true;
        if(flag){
            int i=0;
            flag = false;
            for(i=0;i<skb->len;i++){
                printk(KERN_ERR "%x,",skb->data[i]);
            }
        }
    #endif

    if(start_xmit_first){
        if(skb_head != NULL){
            printk(KERN_ERR "error last skb is not put in ringbuffer\n");
        }
        if(freeSkbList->count > 0){
            skb_head = freeSkbList->ringbuffer[freeSkbList->Used++];
            freeSkbList->Used %= FREE_SKB_MAX;
            freeSkbList->count--;
            UseFree = true;
        }
        else{
            skb_head = (skb_list *)kmalloc(sizeof(skb_list), GFP_KERNEL);
            skb_head->head = (skb_node *)kmalloc(sizeof(skb_node), GFP_KERNEL);
            UseFree = false;
        }
        skb_head->head->skb = skb;
        skb_head->count = 1;
        tmp = skb_head->head;
        start_xmit_first = false;
    } else { 
        if(!UseFree){
            tmp->next = (skb_node *)kmalloc(sizeof(skb_node), GFP_KERNEL);
        }

        tmp = tmp->next;
        tmp->skb = skb; 
        skb_head->count++;
    }
    if (skb_head->count == MAX_SKBUFFS) {
        mod_timer(&my_timer, jiffies - 1);// 立即超时，自动调用回调函数
    } else
        mod_timer(&my_timer, jiffies + MS_TO_JIFFIES(timer_interval_ms));

    // dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

static int mytun_set_mac_address(struct net_device *dev, void *p) {
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

static int mytun_rx_handler(
	struct sk_buff 					*pstskb, 
	struct net_device 				*pstdev,
    struct packet_type 				*pstpt, 
    struct net_device 				*pstorigdev)
{// 接收到数据包直接发回上层
    return NET_RX_SUCCESS;
}

static int Send_thread(void* data){// wait for condition and send data
    int ret;
    loff_t offst = H2C_OFFSET;
    int pack_offset;
    int packNum;
    skb_node *tmp;
    skb_node *Free_next;
    skb_node *Free_cur;
    intrBuf=kmalloc(sizeof(int)*4, GFP_KERNEL);
    clearIntr=kmalloc(sizeof(int)*4, GFP_KERNEL);
    // 将 0xFFFF 存储到缓冲区中
    intrBuf[0] = 0xFF;
    intrBuf[1] = 0xFF;
    intrBuf[2] = 0xFF;
    intrBuf[3] = 0xFF;

    clearIntr[0] = 0x00;
    clearIntr[1] = 0x00;
    clearIntr[2] = 0x00;
    clearIntr[3] = 0x00;
   
    while(1) {
        wait_event_interruptible(my_wait_queue, write_condition);
        write_condition = 0;
        sgl_current = 1 - sgl_current;
        while(writeRingbuffer->WrInx != writeRingbuffer->RdInx) {

            pci_send(xcdev,xdev, offst); 

            offst += PACK_SIZE;
            if(offst >= RINGBUFFER_SIZE+H2C_OFFSET){
                offst = H2C_OFFSET;
            }
            
            tmp = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]->head;
            packNum = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]->count;
            while (packNum--) {
                // TODO 这里不会自动把指针置为空，怎么检验free成功
                kfree_skb(tmp->skb);
                tmp = tmp->next;
            } 
            if(freeSkbList->count < FREE_SKB_MAX){
                freeSkbList->ringbuffer[freeSkbList->Free++] = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx];
                freeSkbList->Free %= FREE_SKB_MAX;
                freeSkbList->count++;
            }
            else{ // 直接free掉申请数量过多的链表
                Free_cur = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]->head;
                while(Free_cur){
                    Free_next = Free_cur->next;
                    kfree(Free_cur);
                    Free_cur = Free_next;
                }
                kfree(writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]);
            }
            // printk(KERN_ERR "send end, offst=%x, pstskb_array[1-sgl_current][0]=%x\n",offst,pstskb_array[1-sgl_current][0]);
            ret = kernel_write(g_stpcidev.h2c0, intrBuf, 4, &intrPos);// send interrupt
            ret = kernel_write(g_stpcidev.h2c0, clearIntr, 4, &intrPos);
            // clear interrupt 这里未来真正实现不需要在这里清中断，只需要在arm侧读清reason就可以达到清中断效果

            writeRingbuffer->RdInx++;
            writeRingbuffer->RdInx %= WriteRingSize;
        }
    }
    // never reach
    return 0;
}

static int Intr_thread(void* data){// 接收中断线程 移动ringbuffer的Write指针，如果ringbuffer为空则通知读ddr线程
    int ret;
    msiData=kmalloc(sizeof(int)*4, GFP_KERNEL);
    msiData[0] = 0xFF;
    msiData[1] = 0xFF;
    msiData[2] = 0xFF;
    msiData[3] = 0xFF;
    
    while(1){// 等待被唤醒，读取中断寄存器，改变ringbuffer指针,若ringbuffer为空，通知read
        wait_event_interruptible(my_wait_queue, Intr_condition);
        Intr_condition = 0;

        // printk(KERN_ERR "debug Intr_thread: Intr_thread is running\n");

        ringbuffer->bWrIx+=1;            
        ringbuffer->bWrIx&=ringbuffer->bMax;            
        // printk(KERN_ERR "debug Intr_thread: ringbuffer->bWrIx:%x,ringbuffer->bRdIx:%x\n",ringbuffer->bWrIx,ringbuffer->bRdIx);

        ret = kernel_write(g_stpcidev.h2c0, msiData, 4, &msiReq);// clear msi interrupt

        // printk(KERN_ERR "debug Intr_thread: clear msi interrupt\n");
        if(((ringbuffer->bRdIx+1)&ringbuffer->bMax)==ringbuffer->bWrIx){// TODO 
            // printk(KERN_ERR "debug Intr_thread: ringbuffer is empty wake up read\n");
            read_condition = 1;
            wake_up_interruptible(&my_wait_queue);
        }

    }
    return 0;
}

static int Receive_thread(void* data){// 读取ddr线程 从ddr读取数据后调用rx-handler发送回上层IP层
    unsigned char* skb_data;
    struct sk_buff *ddr_skb;
    struct iphdr *iph;
    int ret;
    int index = 0;
    loff_t offst = C2H_OFFSET;
    mydev = dev_get_by_name(&init_net, MHYTUN_DEV_NAME);
    if (!mydev) {
        pr_err("Failed to get network device\n");
        return -ENODEV;
    }
    
    while(1){
        wait_event_interruptible(my_wait_queue, read_condition);
        read_condition = 0;
        while(ringbuffer->bRdIx != ringbuffer->bWrIx){
            while(index++<MAX_SKBUFFS){
                ddr_skb = alloc_skb(MTU_SIZE+NET_IP_ALIGN, GFP_KERNEL);
                if (!ddr_skb) {
                    pr_err("error Failed to allocate ddr_skb\n");
                    return -ENOMEM;
                }
                skb_reserve(ddr_skb, NET_IP_ALIGN);
                skb_data = skb_put(ddr_skb, MTU_SIZE);
                ddr_skb->dev = mydev;
                // configSKB(ddr_skb, skb_data, false);
                ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0,skb_data,MTU_SIZE,&offst); 

                // configSKB(ddr_skb, skb_data, true);
                skb_set_network_header(ddr_skb, sizeof(struct ethhdr));// reset or set
                skb_set_transport_header(ddr_skb, sizeof(struct ethhdr) + sizeof(struct iphdr));

                ddr_skb->protocol = eth_type_trans(ddr_skb, mydev); // 设置协议为IP
                ddr_skb->ip_summed = CHECKSUM_UNNECESSARY;
#if 0
                iph = (struct iphdr *)(skb_network_header(ddr_skb));
                if(iph)
                    printk(KERN_ERR "Receive-thread Protocol:%d, len:%d, ipsaddr:%x, daddr:%x\n",iph->protocol, ddr_skb->len, iph->saddr, iph->daddr); 
                else
                    printk(KERN_ERR "debug Receive-thread iph is null");
#endif
                if(ddr_skb){
                    // printk(KERN_ERR "debug Rx-thread ringbuffer->bRead:%x,ringbuffer->bWrIx:%x,offst=%llx,index=%d\n",ringbuffer->bRdIx,ringbuffer->bWrIx,offst,index);
        
                    #if 0 /*debug 打印具体数据 和QT读取一样*/
                        int i=0;
                        static bool flag = true;
                        if(flag){
                            flag = false;
                            printk(KERN_ERR "begin print data,skb->len=%x\n",ddr_skb->len);
                            for(i=0;i<ddr_skb->len;i++){
                                printk(KERN_ERR "%x,",ddr_skb->data[i]);
                            }
                        }
                    #endif
                    if (netif_rx(ddr_skb) != NET_RX_SUCCESS) 
                    {
                        kfree_skb(ddr_skb);
                        printk(KERN_ERR "Rx-thread netif_rx run error\n");
                    }
                }
                else
                    printk(KERN_ERR "Rx-thread ddr_skb is null");
                if(ret<=0)
                    printk(KERN_ERR "Rx-thread read ddr error,ret=%x\n",ret);

                
                offst += MTU_SIZE;
                if(offst>=RINGBUFFER_SIZE+C2H_OFFSET){
                    offst = C2H_OFFSET;
                }
            }
            index=0;
            ringbuffer->bRdIx+=1;
            ringbuffer->bRdIx &= ringbuffer->bMax;
        }
    }
    // never reach
    return 0;
}

static int configSKB(struct sk_buff *ddr_skb, unsigned char* skb_data, bool afterSetData){
    if(afterSetData){
        ddr_skb->dev = mydev;
        ddr_skb->protocol = eth_type_trans(ddr_skb, mydev); // 设置协议为IP
        ddr_skb->ip_summed = CHECKSUM_UNNECESSARY;
        skb_set_network_header(ddr_skb, sizeof(struct ethhdr));// reset or set
        skb_set_transport_header(ddr_skb, sizeof(struct ethhdr)+sizeof(struct iphdr));
    }else{
        ddr_skb = alloc_skb(MTU_SIZE+NET_IP_ALIGN, GFP_KERNEL);
        if (!ddr_skb) {
            pr_err("Failed to allocate ddr_skb\n");
            return -ENOMEM;
        }
        skb_reserve(ddr_skb, NET_IP_ALIGN);
        skb_data = skb_put(ddr_skb, MTU_SIZE);
    }
    return 0;
}

static void Timer_Callback(struct timer_list *timer){// wake up send_thread
    // printk(KERN_ERR "ready wake skb_count[sgl_current]:%d\n",skb_count[sgl_current]);
    writeRingbuffer->ringbuffer[writeRingbuffer->WrInx++] = skb_head;
    writeRingbuffer->WrInx %= WriteRingSize;
    skb_head = NULL;
    start_xmit_first = true; // TODO 这里会不会有问题，需不需要ping pong
    if((writeRingbuffer->RdInx + 1) % WriteRingSize == writeRingbuffer->WrInx){ // ring buffer is empty, wake up send_thread
        write_condition = 1;
        wake_up_interruptible(&my_wait_queue);
    }
    return ;
}

int pci_send(struct xdma_cdev *xcdev, struct xdma_dev *xdev, loff_t offst) {
    int nents, i=0, packNum;
    packNum = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]->count;
    skb_node* tmp = writeRingbuffer->ringbuffer[writeRingbuffer->RdInx]->head;
    sg_init_table(sgl[1-sgl_current], packNum);
    
    for (i = 0; i < packNum; i++) {
        sg_set_buf(&sgl[1-sgl_current][i], tmp->skb->data, MTU_SIZE);// pstskb_array[1-sgl_current][i]->len
        tmp = tmp->next;
    }

    nents = pci_map_sg(pdev, sgl[1-sgl_current], packNum, DMA_TO_DEVICE);
    if (nents == 0) {
        printk(KERN_ERR "pci_send Failed to map scatterlist\n");
        return 1;
    }
    // 设置 sg_table
    sgt.sgl = sgl[1-sgl_current];
    sgt.nents = nents;
    sgt.orig_nents = packNum;
    my_xdma_xfer_submit(xdev,0,offst,&sgt,0);

    pci_unmap_sg(pdev, sgt.sgl, sgt.orig_nents, DMA_TO_DEVICE);
    return 0; 
}


int pcie_init(void)
{
    int status = 0;

    g_stpcidev.h2c0_path = PCIE_DEV_H2C;
    g_stpcidev.c2h0_path = PCIE_DEV_C2H;
    	
    return status;
}

//打开PCIe设备
int pcie_open(void)
{
    int status = 0;
    // mm_segment_t old_fs;

    // old_fs = get_fs();
    // set_fs(KERNEL_DS);

    /* 打开 XDMA Host-to-Card 0 设备 */
    g_stpcidev.h2c0 = filp_open(g_stpcidev.h2c0_path, O_RDWR, S_IRUSR|S_IWUSR);
    if (IS_ERR(g_stpcidev.h2c0)) {
        pr_err("FAILURE: Could not open %s. Make sure g_stpcidev device driver is loaded and you have access rights.\n", g_stpcidev.h2c0_path);
        status = PTR_ERR(g_stpcidev.h2c0);
        goto cleanup_handles;
    }

    /* 打开 XDMA Card-to-Host 0 设备 */
    g_stpcidev.c2h0 = filp_open(g_stpcidev.c2h0_path, O_RDWR, S_IRUSR|S_IWUSR);
    if (IS_ERR(g_stpcidev.c2h0)) {
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

//关闭PCIe设备
void pcie_close(void)
{
    // mm_segment_t old_fs;

    // old_fs = get_fs();
    // set_fs(KERNEL_DS);
	if (g_stpcidev.c2h0) filp_close(g_stpcidev.c2h0,NULL);
    if (g_stpcidev.h2c0) filp_close(g_stpcidev.h2c0,NULL);
    if (g_stpcidev.buffer_c2h) kfree(g_stpcidev.buffer_c2h);
    if (g_stpcidev.buffer_h2c) kfree(g_stpcidev.buffer_h2c);
    // set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
}

static int __init mytun_init(void)
{
    int 				err,i=0,j=0;
    skb_node* tmp;
    err = register_my_notifier(&my_notifier_block);
    if (err) {
        printk(KERN_ERR "Failed to register notifier: %d\n", err);
    }
    ringbuffer = Cl2FifoCreateFifo(RING_BUFF_DEPTH);
    writeRingbuffer = kmalloc(sizeof(WriteRingbuffer), GFP_KERNEL);
    writeRingbuffer->WrInx = 0;
    writeRingbuffer->RdInx = 0;
    freeSkbList = kmalloc(sizeof(FreeSkbList), GFP_KERNEL);
    freeSkbList->Free = 0;
    freeSkbList->Used = 0;
    freeSkbList->count = FREE_SKB_MAX;
    for(i=0;i<FREE_SKB_MAX;i++){
        freeSkbList->ringbuffer[i] = kmalloc(sizeof(skb_list), GFP_KERNEL);
        freeSkbList->ringbuffer[i]->head = kmalloc(sizeof(skb_node), GFP_KERNEL);
        freeSkbList->ringbuffer[i]->count = 0;
        tmp = freeSkbList->ringbuffer[i]->head;
        for(j=0;j<MAX_SKBUFFS - 1;j++){
            tmp->next = kmalloc(sizeof(skb_node), GFP_KERNEL);
            tmp = tmp->next;
        }
    }

    skb_head = NULL;
    g_stmytundev = alloc_netdev(0, MHYTUN_DEV_NAME, NET_NAME_ENUM, ether_setup);
    if ( !g_stmytundev )	{
        printk(KERN_ERR "alloc_error\n" );
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
        printk(KERN_ERR "register error,ret=%x\n",err);
        return err;
    }

    rtnl_lock();
    if (dev_change_flags(g_stmytundev, IFF_UP, NULL) < 0) 
	{
        free_netdev(g_stmytundev);
        return err;
    }

    if (netif_running(g_stmytundev)){
        netif_stop_queue(g_stmytundev);
        dev_close(g_stmytundev);
    }
    

    
    err = dev_set_mac_address(g_stmytundev, &stifrtest.ifr_hwaddr, NULL);
    if ( err < 0) 
	{
        printk(KERN_ERR "set mac addr error err:%d\n",err );
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
    mytun_packet_type.type = htons(ETH_P_ALL);//只接收IP数据包 ETH_P_ALL//接收所有数据包
    mytun_packet_type.func = mytun_rx_handler;
    mytun_packet_type.dev = NULL; // NULL 表示接收所有设备的包
//sgl alloc
    int sgli=0;
    for(;sgli<SGL_NUM;sgli++){
        sgl[sgli] = kzalloc(sizeof(struct scatterlist) * MAX_SKBUFFS, GFP_KERNEL);
        skb_count[sgli] = 0;
    }
// init
    sgl_current = 0;
    pdev = pci_get_device(PCI_VID,PCI_PID,NULL);
//sgl end

// rx-handler
    xcdev = (struct xdma_cdev *)g_stpcidev.h2c0->private_data;
	xdev = xcdev->xdev;
    timer_setup(&my_timer, Timer_Callback, 0);
    send_thread = kthread_run(Send_thread, NULL, "send_thread");
    if (IS_ERR(send_thread)) {
        printk(KERN_ERR "Failed to create thread1\n");
        return PTR_ERR(send_thread);
    } 
    receive_thread = kthread_run(Receive_thread, NULL, "receive_thread");
    if (IS_ERR(receive_thread)) {
        printk(KERN_ERR "Failed to create thread2\n");
        return PTR_ERR(receive_thread);
    } 
    intr_thread = kthread_run(Intr_thread, NULL, "intr_thread");
    if (IS_ERR(intr_thread)) {
        printk(KERN_ERR "Failed to create thread3\n");
        return PTR_ERR(intr_thread);
    } 


#if 1
    cpumask_t cpuset;

    // set every thread to different cpu
    cpumask_clear(&cpuset);
    cpumask_set_cpu(0, &cpuset);  // Read_thread on CPU 0
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
//sgl free
    int sgli=0;
    for(;sgli<SGL_NUM;sgli++){
        kfree(sgl[sgli]);
    }

    // dev_put(g_stpcidev);
    unregister_netdev(g_stmytundev);
    free_netdev(g_stmytundev);
    del_timer(&my_timer);
    if (send_thread) {
        kthread_stop(send_thread);
    }

    if (receive_thread) {
        kthread_stop(receive_thread);
    }

    if (intr_thread) {
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
