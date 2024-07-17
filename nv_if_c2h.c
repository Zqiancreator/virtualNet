/******************************************************************************

  Copyright (C), 2001-2011,  npc , Inc..

 ******************************************************************************
  File Name     : nv_if.c
  Version       : Initial Draft
  Author        : ssh
  Created       : 2024/5/7
  Last Modified :
  Description   : ethnet interface source file
  Function List :
  History       :

*******************************************************************************/

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
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/spinlock.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/icmp.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
                              Macro definitions                               
 ******************************************************************************/
#define MHYTUN_DEV_NAME 		"veth_net1"
#define MHYTUN_IP_ADDR 			"192.168.1.2"
#define DEST_IP_ADDR 			"192.168.1.2"
#define MHYTUN_NETMASK 			"255.255.255.0"
#define MHYTUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x55}
#define FPGATUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x56}
#define ONE_GB                  0x40000000
#define PCIE_DEV                "/dev/xdma0_h2c_0"
#define READ_BASE               0x800000000 // 需要读取的物理地址
#define WRITE_BASE              0x820000000 // 需要写入ddr的物理地址
#define RINGBUFFER_SIZE         ((1<<RING_BUFF_DEPTH)*PACK_SIZE) 
#define MEM_SIZE                0x100000      // 内存区域的大小
#define MAX_SKBUFFS             2               // 单个包中含有的MTU数量 
#define MTU_SIZE                1496
#define PACK_SIZE               MAX_SKBUFFS*MTU_SIZE // TODO
// #define NET_IP_ALIGN            2  // processor.h 中定义为0
#define MSI_ENABLE              0x82000060
#define MSI_INTERRUPT           0x82000068
#define CLEAR_INTR              0x82000070
#define MAC_OFFSET              0 // 14
#define RING_BUFF_DEPTH         6           // ringbuff中含有1<<RING_BUFF_DEPTH 个PACKAGE
#define QUEUE_SIZE              100
#define TIME_OUT                0
#define MS_TO_JIFFIES(ms)       ((ms) * HZ / 1000)
#define READ_ALIGN              8
/*******************************************************************************
                              variable definitions                                
 ******************************************************************************/
unsigned char fpga_mac_src[] = FPGATUN_MAC_ADDR;
unsigned char mhy_mac_src[] = MHYTUN_MAC_ADDR;
static struct packet_type mytun_packet_type;
static struct packet_type pci_packet_type;
static int receiveTimes;
static struct task_struct *Read_thread;
static struct task_struct *Send_thread;
static struct task_struct *Intr_thread;
typedef struct {
    u8 bMax;                 // 环形缓冲区的最大索引    
    u8 bWrIx;                // 写索引    
    u8 bRdIx;                // 读索引    
} Cl2_Packet_Fifo_Type;
static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int read_condition = 0;
static int send_condition = 0;
static int Intr_condition = 0;
static struct net_device 		*g_stmytundev;

static struct ifreq 		stifrtest;

// kmap
static struct page *page;
static void *vaddr;
static unsigned long pfn;  // 计算页帧号
unsigned int *read_buff;
struct sk_buff *ddr_skb;
unsigned char *skb_data;

struct net_device *mydev;
static spinlock_t tx_lock;
static int tx_queue_head = 0;
static int tx_queue_tail = 0;
static struct sk_buff *tx_queue[QUEUE_SIZE];
static struct timer_list my_timer;
static unsigned long timer_interval_ms = TIME_OUT; // 定时器间隔（毫秒）
// sendthread and timer callback
unsigned int send_thread_offst=0;
unsigned int sendNum = 0;

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

/* Assuming PCIe device */
/*******************************************************************************
                         	  Local function declarations                          
 ******************************************************************************/
static int read_thread(void* data);
static int send_thread(void* data);
static int read_ddr(int offst, int read_size);
static int write_ddr(struct sk_buff *skb, int offst,int write_size);
static int configSKB(bool afterSetData);
static void Timer_Callback(struct timer_list *timer);
static unsigned int RWreg(unsigned long long BaseAddr, int value, int rw);
/*******************************************************************************
                         	  extern function declarations                          
 ******************************************************************************/
extern int register_my_notifier(struct notifier_block *nb);
extern int unregister_my_notifier(struct notifier_block *nb);

// 声明外部变量
extern Cl2_Packet_Fifo_Type* ringbuffer;
/*******************************************************************************
                         	  extern function usage                          
 ******************************************************************************/
    // 通知处理函数
    static int my_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
    {// 等待中断处理函数通知(此时RingBuffer已经为空),然后唤醒读取ddr的线程
        start = ktime_get();
        // printk(KERN_ERR "Notification received success!, data:%s\n",(char*)data);
        if(read_condition == 0){
            read_condition = 1;
            wake_up_interruptible(&my_wait_queue);
        }else{
            printk(KERN_ERR "error notify too fast\n");
        }
        return NOTIFY_OK;
    }

    // 定义一个通知块
    static struct notifier_block my_notifier_block = {
        .notifier_call = my_notifier_call,
    };
/*******************************************************************************
                        	  Global function declarations                          
 ******************************************************************************/
/*******************************************************************************
                       Inline function implementations                        
*******************************************************************************/
static void Timer_Callback(struct timer_list *timer){// 发送中断操作不能在中断处理函数中完成，
        Intr_condition = 1;
        wake_up_interruptible(&my_wait_queue);// 唤醒发送中断线程
        if(sendNum != MAX_SKBUFFS){
            send_thread_offst = (send_thread_offst+(MAX_SKBUFFS-sendNum)*MTU_SIZE)%RINGBUFFER_SIZE;
            // printk(KERN_ERR "not full sendNum=%d, after change send_thread_offst=%x\n",sendNum, send_thread_offst);
        }
        sendNum = 0;
        return ;
}

static int intr_thread(void *data){
    while(1){
        wait_event_interruptible(my_wait_queue, Intr_condition);
        Intr_condition=0;
        // printk(KERN_ERR "debug msi interrupt\n");
        // RWreg(MSI_ENABLE, 0xabababab, 1); // enable write msi interrupt
        RWreg(MSI_INTERRUPT, 0x01, 1);        // send msi interrupt
        // RWreg(MSI_INTERRUPT, 0x01, 0);        // send msi interrupt
        // ssleep(1);
        // RWreg(MSI_INTERRUPT, 0x0000, 0);        // read msi interrupt 
        // RWreg(CLEAR_INTR, 0, 0);        // clear intr
        // printk(KERN_ERR "send msi interrupt\n");

    }     
}
/*******************************************************************************
                       Local function implementations                         
*******************************************************************************/
static int send_thread(void *data){// write to ddr and send interrupt
    struct sk_buff *skb;
    RWreg(MSI_ENABLE, 0xabababab, 1); // enable write msi interrupt
    while(1){
        wait_event_interruptible(my_wait_queue, send_condition);
        send_condition=0;
        // 处理发送队列中的数据包
        while (tx_queue_head != tx_queue_tail) {
            mod_timer(&my_timer, jiffies + MS_TO_JIFFIES(1)); // TODO 留1ms来写入ddr
            skb = tx_queue[tx_queue_head];
            if(!skb){
                printk(KERN_ERR "error skb is null, tx queue head=%d\n",tx_queue_head);
            }

            #if 0 /*debug 打印具体数据*/
                int i=0;
                printk(KERN_ERR "send thread begin print data,skb->len=%x,tx_queue_head=%x\n",skb->len,tx_queue_head);
                for(i=0;i<skb->len;i++){
                    printk(KERN_ERR "%x,",skb->data[i]);
                }
            #endif
            start3 = ktime_get();
            // 实际发送数据包 发送一个包
            write_ddr(skb, send_thread_offst, MTU_SIZE);
            
#if 0
            end3 = ktime_get();
            delta3 = ktime_to_ns(ktime_sub(end3, start3));
            delta4 = ktime_to_ns(ktime_sub(end4, start4));
            printk(KERN_INFO "write MTU took %lld ns to execute.\n", delta3);
            printk(KERN_INFO "inside write MTU took %lld ns to execute.\n", delta4);
#endif
            // 更新环形缓冲区的读写索引 允许上层继续调用 start_xmit
            tx_queue_head = (tx_queue_head + 1) % QUEUE_SIZE;
            if (netif_queue_stopped(mydev)){
                // printk(KERN_ERR "wake up dev, start send\n");
                netif_wake_queue(mydev);// TODO wake up dev 待验证
            }
            
            // printk(KERN_ERR "send thread send skb, send_thread_offst=%x,tx_queue_head=%x\n",send_thread_offst,tx_queue_head);
            send_thread_offst = (send_thread_offst+MTU_SIZE)%RINGBUFFER_SIZE;
            // printk(KERN_ERR "after change send_thread_offst, send_thread_offst=%x,tx_queue_head=%x\n",send_thread_offst,tx_queue_head);

            if(++sendNum == MAX_SKBUFFS){
                mod_timer(&my_timer, jiffies - 1);// 立即超时，自动调用回调函数发送中断
            }else
                mod_timer(&my_timer, jiffies + MS_TO_JIFFIES(timer_interval_ms)); // 设置下一个包到来的超时时间

            dev_kfree_skb(skb);
        }
        
    }
}

static int read_thread(void *data)
{// 一直等待直到收到buffer为空的消息
    static u32 read_offset = 0;
    int ret;
    struct iphdr *iph;// debug, check header
    read_offset = ringbuffer->bRdIx*PACK_SIZE;// TODO
    mydev = dev_get_by_name(&init_net, MHYTUN_DEV_NAME);
    if (!mydev) {
        pr_err("Failed to get network device\n");
        return -ENODEV;
    }

    while(1){
#if 0
        start6 = ktime_get();
        printk(KERN_ERR "read_thread once took %lld ns to execute\n", delta5);
        end6 = ktime_get();
        delta6 = ktime_to_ns(ktime_sub(end6, start6));
        printk(KERN_ERR "one printk took %lld ns to execute\n", delta6);
        start5 = ktime_get();
#endif
        wait_event_interruptible(my_wait_queue, read_condition);
        while(ringbuffer->bRdIx!=ringbuffer->bWrIx){
            // printk(KERN_ERR "read read_offset=%x, RdIx=%x, WrIx=%x\n",read_offset, ringbuffer->bRdIx, ringbuffer->bWrIx);
            if(read_ddr(read_offset, PACK_SIZE) == -1){
                printk(KERN_ERR "read_thread: read ddr error\n");
                break; 
            }
            // printk(KERN_ERR "read_offset=%x, RINGBUFFER_SIZE=%x\n",read_offset, RINGBUFFER_SIZE);
            read_offset = (read_offset+PACK_SIZE)%(RINGBUFFER_SIZE);
            ringbuffer->bRdIx += 1;
            ringbuffer->bRdIx &= ringbuffer->bMax;
        }
#if 0
        end5 = ktime_get();
        delta5 = ktime_to_ns(ktime_sub(end5, start5));
#endif

        read_condition = 0;
    }
    // never reach
    return 0;
    
}

static int configSKB(bool afterSetData){
    if(afterSetData){
        ddr_skb->dev = mydev;
        skb_set_network_header(ddr_skb, sizeof(struct ethhdr));// reset or set
        skb_set_transport_header(ddr_skb, sizeof(struct ethhdr)+sizeof(struct iphdr));

        ddr_skb->protocol = eth_type_trans(ddr_skb, mydev); // 设置协议为IP
        ddr_skb->ip_summed = CHECKSUM_UNNECESSARY;
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

static int mytun_open(struct net_device *dev) {
    netif_start_queue(dev);
    return 0;
}

static int mytun_stop(struct net_device *dev) {
    netif_stop_queue(dev);
    return 0;
}

static netdev_tx_t mytun_start_xmit(struct sk_buff *skb, struct net_device *dev) {// 收到上层发送请求唤醒sendthread去写ddr和发中断
    // Transmit packet logic here
        struct iphdr *  iph = (struct iphdr *)skb_network_header(skb);
        #if 0
        if(iph->daddr==0xff18a8c0)// 忽略广播包
            return NETDEV_TX_OK;
        if(skb->protocol==0xdd86)// 忽略ipv6包
            return NETDEV_TX_OK;
        #endif
        if(iph&&iph->saddr!=0){
            #if 0 /*debug 打印具体数据*/
                static bool flag = true;
                if(flag){
                    int i=0;
                    flag=false;
                    printk(KERN_ERR "begin print data,skb->len=%x\n",skb->len);
                    for(i=0;i<skb->len;i++){
                        printk(KERN_ERR "%x,",skb->data[i]);
                    }
                }
            #endif
            // printk(KERN_ERR "debug ready xmit: iph->saddr=%x,iph->daddr=%x,skb->protocol=%x,iph->protocol=%x\n",iph->saddr,iph->daddr,skb->protocol,iph->protocol);
        }
        else{
            printk(KERN_ERR " iph is null or saddr is 0\n");
            // if(iph)
            //     printk(KERN_ERR "xmit iph->daddr=%x,skb->protocol=%x,iph->protocol=%x\n",iph->daddr,skb->protocol,iph->protocol);
            return NETDEV_TX_OK;
        }

    // end2 = ktime_get();
    // delta2 = ktime_to_ns(ktime_sub(end2, start2));
    // printk(KERN_INFO "net process pack and reply took %lld ns to execute.\n", delta2);

    // 将数据包添加到发送队列 TODO 判满
    tx_queue[tx_queue_tail] = skb;
    tx_queue_tail = (tx_queue_tail + 1) % QUEUE_SIZE;


    send_condition = 1;
    wake_up_interruptible(&my_wait_queue);// 唤醒实际发送线程

    // 检查发送队列是否已满
    if (tx_queue_tail  == tx_queue_head) {
        // 停止发送队列，防止上层继续调用 start_xmit
        printk(KERN_ERR "queue is full, stop send\n");
        netif_stop_queue(dev);
        return NETDEV_TX_BUSY;// 让上层暂停调用
    }
    // 返回 NETDEV_TX_OK 表示数据包已成功发送或已被处理
    return NETDEV_TX_OK;

    return NET_RX_DROP;
}

static int mytun_set_mac_address(struct net_device *dev, void *p) {
    struct sockaddr *addr = p;
    // 在这里编写实际改变 MAC 地址的代码
    // ...
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


/*****************************************************************************
 Prototype      : pci_rx_handler
 Description    : 处理pcie的数据
 Input          : *pstskb----指向数据结构体的地址 
 Output         : None
 Return Value   : 成功或失败
 Calls          : 
 Called By      : 
 
 History        :
 1.Date         : 2024/5/7
   Author       : ssh
   Modification : Created function

*****************************************************************************/
static int pci_rx_handler(
	struct sk_buff 					*pstskb, 
	struct net_device 				*pstdev,
    struct packet_type 				*pstpt, 
    struct net_device 				*pstorigdev)
{// 收到的数据直接发到上层ip层

    return NET_RX_SUCCESS;

    struct iphdr *iph; 
    struct ethhdr *eth;
    if(pstskb->protocol==0xdd86)// 忽略ipv6包
        return NET_RX_DROP;

    iph = (struct iphdr *)skb_network_header(pstskb);
    if (!iph) {
        printk(KERN_ERR "Invalid IP header\n");
        return NET_RX_DROP;
    }
    #if 0
    if (netif_rx(pstskb) == NET_RX_SUCCESS) {// 内核接管，自动free
        #if 0 /* 这里如果放开会导致一直接收包 iph->saddr=60dd86,iph->daddr=24000000,skb->protocol=dd86,iph->protocol=33  iph->saddr=450008,iph->daddr=cd8f0201,skb->protocol=8,iph->protocol=33*/
        if((recv&&iph->saddr==0x60dd86)||(recv2&&iph->saddr==0x450008)){
            if(iph->saddr==0x60dd86)
                recv = false;
            else
                recv2 = false;
            printk(KERN_ERR "debug2 rx-handler: iph->saddr=%x,iph->daddr=%x,skb->protocol=%x,iph->protocol=%x\n",iph->saddr,iph->daddr,pstskb->protocol,iph->protocol);
            printk(KERN_ERR "netif_rx run success,pstskb->len:%x\n",pstskb->len);
            for(i=0;i<pstskb->len;i++){
                printk(KERN_ERR "pstskb->data[%d]:%x,",i,pstskb->data[i]);
            }
        }
        #endif
        return 0;
    }else{
        printk(KERN_ERR "rx-handler Protocol:%d, len:%d, ipsaddr:%x, daddr:%x\n",iph->protocol, pstskb->len, iph->saddr, iph->daddr);
        printk(KERN_ERR "rx-handler netif_rx run error\n"); 
    }
    #endif 
    
    return NET_RX_SUCCESS;
    return NET_RX_DROP;
}

static unsigned int RWreg(unsigned long long BaseAddr, int value, int rw){
    void __iomem *vaddr;
    unsigned int readres = 0;
    int offset = BaseAddr & ~PAGE_MASK;
    BaseAddr &= PAGE_MASK;

    // printk(KERN_ERR "before ioremap,BaseAddr=%x, offset=%x\n",BaseAddr, offset);
    // 使用 ioremap 映射物理地址
    vaddr = ioremap(BaseAddr, PAGE_SIZE);
    if (!vaddr) {
        pr_err("Failed to ioremap physical address 0x%llx\n", BaseAddr);
        return -ENOMEM;
    }

    // printk(KERN_ERR "after ioremap,BaseAddr=%x, offset=%x\n",BaseAddr, offset);
    // 访问映射的内存
    if(rw){
        iowrite32(value, vaddr+offset);
        // printk(KERN_ERR "Written value=0x%x to address 0x%llx\n", value, BaseAddr+offset);
    }else{
        readres = ioread32(vaddr+offset);
        // printk(KERN_ERR "read value=0x%x from address 0x%llx\n", readres, BaseAddr+offset);
    }

    // 取消映射
    iounmap(vaddr);
    return readres;
}

void flush_dcache(void *start, size_t size)
{
    uintptr_t addr = (uintptr_t)start;
    uintptr_t end = addr + size;

    // 确保内存操作顺序
    asm volatile ("dsb ish");

    while (addr < end) {
        asm volatile ("dc civac, %0" : : "r" (addr) : "memory");
        addr += 64; // 假设缓存行大小为 64 字节
    }

    // 确保所有缓存操作完成
    asm volatile ("dsb ish");
    asm volatile ("isb");
}

// 缓存失效函数
void invalidate_cache(void *start, size_t size) {
    uintptr_t addr = (uintptr_t)start;
    uintptr_t end = addr + size;

    // 确保内存操作的顺序
    asm volatile ("dsb sy" ::: "memory");
    addr = addr & ~(64 - 1);

    while (addr < end) { 
        asm volatile ("dc ivac, %0" :: "r"(addr) : "memory"); 
        addr += 64; 
    }

    // 确保所有缓存操作完成
    asm volatile ("dsb sy" ::: "memory");
    asm volatile ("isb" ::: "memory");
}

static int write_ddr(struct sk_buff *skb,int offst,int write_size){
    // write one skb data,假设现在每次写的数量不超过一页，1500<4096 即 write_size < PAGE_SIZE
    start4 = ktime_get();
    uint32_t current_char;
    unsigned int Page_index = 0;
    int ret = 0;
    unsigned int index = 0;
    int currentSize; // 下一页要写的数据 
    struct iphdr *iph;
    unsigned long pfn;
    void *vaddr;
    struct page *page;
    pfn = (WRITE_BASE + offst) >> PAGE_SHIFT;
    Page_index = offst % PAGE_SIZE;
    write_size += Page_index;
    currentSize = write_size > PAGE_SIZE? write_size - PAGE_SIZE:0;
    int testPageIndex =Page_index;
#if 0
    iph = (struct iphdr *)(skb_network_header(skb));
    printk(KERN_ERR "debug write ddr Protocol:%d, len:%d, ipsaddr:%x, daddr:%x,offst=%x\n",iph->protocol, skb->len, iph->saddr, iph->daddr,offst);
    printk(KERN_ERR "debug write ddr pfn:%d, PageIndex:%d, writeSize:%x, currentSize:%x,offst=%x\n",pfn, Page_index, write_size, currentSize,offst);
#endif
    while (write_size > 0) {
        page = pfn_to_page(pfn);
        if (!page) {
            pr_err("Failed to get page for PFN 0x%lx\n", pfn);
            return -1;
        }

        // 映射页到内核地址空间
        vaddr = kmap(page);
        if (!vaddr) {
            pr_err("Failed to map page\n");
            return -1;
        }

        // 访问内存
        for (; Page_index < write_size && Page_index < PAGE_SIZE; index += 4, Page_index += 4 ) {
            // current_char = *((unsigned int *)(skb->data + index));
            // *(volatile unsigned int *)(vaddr + Page_index) = current_char;
            // *(unsigned int *)(vaddr + Page_index) = current_char;

            *(unsigned int *)(vaddr + Page_index) = *((unsigned int *)(skb->data + index));

        }

        
        mb();  // Memory barrier before write
        // flush_dcache_page(page);             // 清除缓存，使得ddr数据真正写入
        flush_dcache(vaddr, PAGE_SIZE);
        mb();  // Memory barrier after write
        // 取消映射页
        kunmap(page);

        // 翻页处理
        if(Page_index == PAGE_SIZE){
            write_size = currentSize; 
            Page_index = 0;
            // printk(KERN_ERR "next page write_size=%x\n",write_size);
        }else if(Page_index == write_size){
            // printk(KERN_ERR "Page_index=write_size=%x\n",write_size);
            break;
        }
        else{
            printk(KERN_ERR "ERROR Page_index = 0x%x,write_size=%x\n",Page_index,write_size);
            break;
        }
        pfn += 1;
        // printk(KERN_ERR "next page\n");
    }
    // printk(KERN_ERR "write success\n");
    end4 = ktime_get();
    return 0;
}

static int read_ddr(int offst,int read_size){// read_size>PAGE_SIZE   TODO offset,read_size 类型
    uint64_t current_char;
    unsigned int index=0;
    unsigned int Page_index;
    int TotalSize = 0;
    int ret = 0;
    int currentSize;
    int packNum = 0;
    struct iphdr *iph; 
    
    pfn = (READ_BASE+offst) >> PAGE_SHIFT;
    Page_index = offst % PAGE_SIZE;
    currentSize = PAGE_SIZE - Page_index; // 当前这页可以读取的数据
    // printk(KERN_ERR "read ddr, offst=%x, read_size=%x,currentSize=%x\n",offst, read_size, currentSize);

    // configSKB(false); 
    #if 1
        ddr_skb = alloc_skb(MTU_SIZE+NET_IP_ALIGN, GFP_KERNEL);
        if (!ddr_skb) {
            pr_err("Failed to allocate ddr_skb\n");
            return -ENOMEM;
        }
        skb_reserve(ddr_skb, NET_IP_ALIGN);
        skb_data = skb_put(ddr_skb, MTU_SIZE);
    #endif
    while(read_size>0&&packNum<MAX_SKBUFFS){

        page = pfn_to_page(pfn);
        if (!page) {
            pr_err("Failed to get page for PFN 0x%lx\n", pfn);
            return -1;
        }

        // 映射页到内核地址空间
        vaddr = kmap(page);
        if (!vaddr) {
            pr_err("Failed to map page\n");
            return -1;
        }
        asm volatile ("dsb ish" : : : "memory"); 
        invalidate_cache(vaddr, PAGE_SIZE); // 无效硬件缓存，使得数据从内存中读取
        // 访问内存
        for(;Page_index<PAGE_SIZE;index+=READ_ALIGN,Page_index+=READ_ALIGN,TotalSize+=READ_ALIGN){
            #if 0 /*ldxr需要八字节对齐 ldxr指令能保证原子性但开销更大*/
                if((unsigned long)vaddr%8!=0)
                    printk(KERN_ERR "error vaddr=%p\n",vaddr);
                asm volatile ("ldxr %0, [%1]" : "=&r" (current_char) : "r" (vaddr + Page_index)); 
            #endif
            // current_char = *(volatile uint64_t *)(vaddr + Page_index);
            // *((uint64_t *)(skb_data + index)) = current_char;
            *((uint64_t *)(skb_data + index)) = *(volatile uint64_t *)(vaddr + Page_index);
            if(index == MTU_SIZE-READ_ALIGN){// 读取到一个完整skb,发回上层 
            asm volatile ("dsb ish" : : : "memory"); 
                // printk(KERN_ERR "read ddr send skb, index=%x,TotalSize=%x,ddr_skb->len=%x\n",index,TotalSize,ddr_skb->len);
                index = -READ_ALIGN;
                
                configSKB(true);

                iph = (struct iphdr *)(skb_network_header(ddr_skb));
                
                if(ddr_skb){
                    if (netif_rx(ddr_skb) == NET_RX_SUCCESS) {
                        #if 0 /*debug 打印目标地址不对的包具体数据*/
                            if(iph->daddr!=0xd18a8c0){
                                int i=0;
                                printk(KERN_ERR "read ddr print data,skb->len=%x\n",ddr_skb->len);
                                for(i=0;i<ddr_skb->len;i++){
                                    printk(KERN_ERR "data=%x,i=%d",ddr_skb->data[i],i);
                                }
                            }
                        #endif
#if 0
                        if(iph->daddr==0xd18a8c0)
                            start2 = ktime_get();
                        end = ktime_get();
                        delta = ktime_to_ns(ktime_sub(end, start));
                        printk(KERN_INFO "read MTU and netif_rx took %lld ns to execute.\n", delta);
#endif
                        // printk(KERN_ERR "read ddr netif_rx run success: iph->saddr=%x,iph->daddr=%x,skb->protocol=%x,iph->protocol=%x, Value at address: 0x%lx\n",iph->saddr,iph->daddr,ddr_skb->protocol,iph->protocol, READ_BASE + offst + TotalSize + READ_ALIGN - MTU_SIZE);
                    }else
                        printk(KERN_ERR "read ddr netif_rx run error\n");
                }
                else
                    printk(KERN_ERR "ddr_skb is null");
                 
                // printk(KERN_ERR "debug end of send skb\n");
                ddr_skb = alloc_skb(MTU_SIZE+NET_IP_ALIGN, GFP_KERNEL);
                if (!ddr_skb) {
                    pr_err("Failed to allocate ddr_skb\n");
                    return -ENOMEM;
                }
                skb_reserve(ddr_skb, NET_IP_ALIGN);
                skb_data = skb_put(ddr_skb, MTU_SIZE);
                // configSKB(false);

                if(++packNum==MAX_SKBUFFS){
                    break;
                }
            }
        }

        // 取消映射页
        kunmap(page);
        // 翻页处理
        pfn += 1;
        Page_index = 0;
        read_size -= currentSize;
        currentSize = read_size>PAGE_SIZE?PAGE_SIZE:read_size;
        // printk(KERN_ERR "next page, read_size=%x,currentSize=%x\n",read_size,currentSize);
    }
    // printk(KERN_ERR "read ddr success,readsize=%x,index=%x\n",read_size,index);
    kfree(ddr_skb); 
    return 0;
}

/*****************************************************************************
 Prototype      : mytun_init
 Description    : mytun网卡初始化、注册
 Input          : None 
 Output         : None
 Return Value   : 成功或失败
 Calls          : 
 Called By      : 
 
 History        :
 1.Date         : 2024/5/7
   Author       : ssh
   Modification : Created function

*****************************************************************************/
static int __init mytun_init(void)
{
    int 				err;
    err = register_my_notifier(&my_notifier_block);
    if (err) {
        printk(KERN_ERR "Failed to register notifier: %d\n", err);
    }
    printk(KERN_ERR "module_init\n" );
    g_stmytundev = alloc_netdev(0, MHYTUN_DEV_NAME, NET_NAME_ENUM, ether_setup);
    if ( !g_stmytundev )	{
        printk(KERN_ERR "alloc_error\n" );
        return -ENOMEM;
    }
    timer_setup(&my_timer, Timer_Callback, 0);

    g_stmytundev->netdev_ops = &mytun_netdev_ops;
    g_stmytundev->flags |= IFF_NOARP;
    g_stmytundev->features |= NETIF_F_IP_CSUM; // NETIF_F_NO_CSUM;
    memcpy(g_stmytundev->dev_addr, fpga_mac_src, ETH_ALEN);

    strcpy(stifrtest.ifr_name, MHYTUN_DEV_NAME);
    stifrtest.ifr_flags = IFF_UP;
    memcpy(stifrtest.ifr_hwaddr.sa_data, fpga_mac_src, ETH_ALEN);
    stifrtest.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    // printk(KERN_ERR "sa_family: %d\n", stifrtest.ifr_hwaddr.sa_family);

    // printk(KERN_ERR "before register_net\n"); 
    err = register_netdev(g_stmytundev);
    if (err) 
	{
        return err;
    }

    // printk(KERN_ERR "register_net, err:%x\n", err);
    rtnl_lock();
    if (dev_change_flags(g_stmytundev, IFF_UP, NULL) < 0) 
	{
        free_netdev(g_stmytundev);
        return err;
    }
    // printk(KERN_ERR "change_flag\n");

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

    // printk(KERN_ERR "set_mac_addr\n" );
    /* Set IP address and netmask */
    struct in_device *in_dev = __in_dev_get_rtnl(g_stmytundev);
    if (!in_dev) 
	{
        printk(KERN_ERR "error init Failed to get in_device\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }
    // printk(KERN_ERR "init in_dev\n" );

    // printk(KERN_ERR "TUN device created: %s\n", MHYTUN_DEV_NAME);
    
    /* 初始化 packet_type 结构体 */
    memset(&pci_packet_type, 0, sizeof(struct packet_type));
    pci_packet_type.type = htons(ETH_P_ALL);
    pci_packet_type.func = pci_rx_handler;
    pci_packet_type.dev = NULL; // NULL 表示接收所有设备的包

    Read_thread=kthread_run(read_thread, NULL, "read_thread");
    Send_thread=kthread_run(send_thread, NULL, "send_thread");
    Intr_thread=kthread_run(intr_thread, NULL, "intr_thread");
#if 1
    cpumask_t cpuset;

    // set every thread to different cpu
    cpumask_clear(&cpuset);
    cpumask_set_cpu(0, &cpuset);  // Read_thread on CPU 0
    set_cpus_allowed_ptr(Read_thread, &cpuset);

    cpumask_clear(&cpuset);
    cpumask_set_cpu(1, &cpuset);  
    set_cpus_allowed_ptr(Send_thread, &cpuset);

    cpumask_clear(&cpuset);
    cpumask_set_cpu(2, &cpuset);  
    set_cpus_allowed_ptr(Intr_thread, &cpuset);
#endif
    dev_add_pack(&pci_packet_type);
    if (err) 
	{
        printk(KERN_ERR "Failed to add packet handler for PCIe\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }

    return 0;
}

/*****************************************************************************
 Prototype      : mytun_exit
 Description    : mytun网卡退出
 Input          : None 
 Output         : None
 Return Value   : 成功或失败
 Calls          : 
 Called By      : 
 
 History        :
 1.Date         : 2024/5/7
   Author       : ssh
   Modification : Created function

*****************************************************************************/
static void __exit mytun_exit(void)
{
    int ret;
    dev_remove_pack(&mytun_packet_type);
    dev_remove_pack(&pci_packet_type);
    unregister_netdev(g_stmytundev);
    free_netdev(g_stmytundev);
    del_timer(&my_timer);
    ret = unregister_my_notifier(&my_notifier_block);
    if (ret) {
        printk(KERN_ERR "Failed to register notifier: %d\n", ret);
    }
    printk(KERN_INFO "TUN device removed: %s\n", MHYTUN_DEV_NAME);
}

module_init(mytun_init);
module_exit(mytun_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
#ifdef __cplusplus
}
#endif
