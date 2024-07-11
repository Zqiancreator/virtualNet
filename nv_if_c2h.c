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
#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
                              Macro definitions                               
 ******************************************************************************/
#define MHYTUN_DEV_NAME 		"mytun"
#define MHYTUN_IP_ADDR 			"192.168.1.2"
#define MHYTUN_NETMASK 			"255.255.255.0"
#define MHYTUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x55}
#define FPGATUN_MAC_ADDR 		{0x02, 0x11, 0x22, 0x33, 0x44, 0x56}
#define ONE_GB                  0x40000000
#define PCIE_DEV                "/dev/xdma0_h2c_0"
#define MEM_BASE 0x20000000 // 需要读取的物理地址
#define MEM_SIZE 0x100      // 内存区域的大小
/*******************************************************************************
                              Type definitions                                
 ******************************************************************************/
unsigned char fpga_mac_src[] = FPGATUN_MAC_ADDR;
unsigned char mhy_mac_src[] = MHYTUN_MAC_ADDR;
static struct packet_type mytun_packet_type;
static struct packet_type pci_packet_type;
static int receiveTimes;
/*******************************************************************************
                         	  Local function declarations                          
 ******************************************************************************/
int pci_send(void *data, size_t len);
int pcie_init(void);
int pcie_open(void);
void pcie_close(void);
/*******************************************************************************
                        	  Global function declarations                          
 ******************************************************************************/
    static struct net_device 		*g_stmytundev;
    static void __iomem *io_base;
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
    static struct net_device 		*g_stpcidev; 
    xdma_device xdma;
/*******************************************************************************
                       Inline function implementations                        
*******************************************************************************/

/*******************************************************************************
                       Local function implementations                         
*******************************************************************************/
static int mytun_open(struct net_device *dev) {
    netif_start_queue(dev);
    return 0;
}

static int mytun_stop(struct net_device *dev) {
    netif_stop_queue(dev);
    return 0;
}

static netdev_tx_t mytun_start_xmit(struct sk_buff *skb, struct net_device *dev) {
    // Transmit packet logic here
    dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

static int mytun_set_mac_address(struct net_device *dev, void *p) {
    struct sockaddr *addr = p;
    // 在这里编写实际改变 MAC 地址的代码
    // ...
    printk(KERN_INFO "Setting MAC address to %pM\n", addr->sa_data); // 举例输出
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
 Prototype      : mytun_rx_handler
 Description    : 处理网络层的数据
 Input          : *pstskb----指向数据结构体的地址 
				  1.从网络层接收数据包 skb。
				  2.使用 skb_push 添加以太网头部。
				  3.设置以太网头部的源 MAC 地址和目的 MAC 地址。
				  4.准备将数据发送到 PCIe 设备。
				  5.调用 pci_send 函数将数据发送到 PCIe 设备。
				  6.释放 skb。
 Output         : None
 Return Value   : 成功或失败
 Calls          : 
 Called By      : 
 
 History        :
 1.Date         : 2024/5/7
   Author       : ssh
   Modification : Created function

*****************************************************************************/
static int mytun_rx_handler(
	struct sk_buff 					*pstskb, 
	struct net_device 				*pstdev,
    struct packet_type 				*pstpt, 
    struct net_device 				*pstorigdev)
{
    struct ethhdr 					*psteth;
    void 							*ppcidata;
    size_t							 pci_data_len;

    if (pstdev == g_stmytundev && pstskb) 
	{
        /* Add Ethernet header */
        skb_push(pstskb, ETH_HLEN);

        /* Set MAC header */
        psteth = (struct ethhdr *)pstskb->data;
        memcpy(psteth->h_source, mhy_mac_src, ETH_ALEN);
		
        /* 设置目标 MAC 地址 */
        memcpy(psteth->h_dest, fpga_mac_src, ETH_ALEN);
        psteth->h_proto = htons(ETH_P_IP);

        /* 准备将数据发送到 PCIe 设备 */
        ppcidata 		= pstskb->data;
        pci_data_len 	= pstskb->len;

        /* 调用函数将数据发送到 PCIe 设备 */
        if (pci_send(ppcidata, pci_data_len) != 0)
		{
			/* 发送失败时释放 skb */
            kfree_skb(pstskb); 
            return NET_RX_DROP;
        }

		/* 成功发送后释放 skb */
        kfree_skb(pstskb); 
        return NET_RX_SUCCESS;
    }

    return NET_RX_DROP;
}

/*****************************************************************************
 Prototype      : pci_send
 Description    : 处理网络层的数据
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
int pci_send(void *data, size_t len)
{
    void *pci_buffer = vmalloc(len);
    if (!pci_buffer) 
	{
        printk(KERN_ERR "Failed to allocate PCIe buffer\n");
        return -ENOMEM;
    }

    memcpy(pci_buffer, data, len);

    // /* 假设 pcie_send_buffer 是实际发送数据到 PCIe 的函数 */
    int ret = xdma.h2c0->f_op->write(xdma.h2c0,pci_buffer,len,&xdma.h2c0->f_pos); 
	// /* 释放 PCIe 缓冲区 */
    // pcie_free_buffer(pci_buffer); 
    kfree(pci_buffer);
    return ret;
}

int pcie_init(void)
{
    int status = -1;

    xdma.h2c0_path = PCIE_DEV;
    xdma.c2h0_path = "/dev/xdma0_c2h_0";

    xdma.buf_c2h_size =(u_int64_t) 2 * ONE_GB;     // buffer card to host size
   // GFP_KERNEL是分配标志，表示正常的内核内存分配方式
    // __GFP_ZERO表示分配的内存会被置零
    xdma.buffer_c2h = kmalloc(xdma.buf_c2h_size, GFP_KERNEL | __GFP_ZERO);
    if (!xdma.buffer_c2h) {
        printk(KERN_ERR "Error allocating buffer\n");
        kfree(xdma.buffer_c2h);
        return -ENOMEM; // 返回内存不足的错误码
    }	
    return status;
}
//打开PCIe设备
int pcie_open(void)
{
    int status = 1;
    mm_segment_t old_fs;

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    /* 打开 XDMA Host-to-Card 0 设备 */
    xdma.h2c0 = filp_open(xdma.h2c0_path, O_RDWR, S_IRUSR|S_IWUSR);
    if (IS_ERR(xdma.h2c0)) {
        pr_err("FAILURE: Could not open %s. Make sure xdma device driver is loaded and you have access rights.\n", xdma.h2c0_path);
        status = PTR_ERR(xdma.h2c0);
        goto cleanup_handles;
    }

    /* 打开 XDMA Card-to-Host 0 设备 */
    xdma.c2h0 = filp_open(xdma.c2h0_path, O_RDWR, S_IRUSR|S_IWUSR);
    if (IS_ERR(xdma.c2h0)) {
        pr_err("FAILURE: Could not open %s.\n", xdma.c2h0_path);
        status = PTR_ERR(xdma.c2h0);
        goto cleanup_handles;
    }

    /* 恢复原始的地址空间 */
    set_fs(old_fs);

    return status;

    cleanup_handles:
    if (!IS_ERR(xdma.c2h0) && xdma.c2h0)
        filp_close(xdma.c2h0, NULL);
    if (!IS_ERR(xdma.h2c0) && xdma.h2c0)
        filp_close(xdma.h2c0, NULL);
    set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
    return status;

}

//关闭PCIe设备
void pcie_close(void)
{
    mm_segment_t old_fs;

    old_fs = get_fs();
    set_fs(KERNEL_DS);
	if (xdma.c2h0) filp_close(xdma.c2h0,NULL);
    if (xdma.h2c0) filp_close(xdma.h2c0,NULL);
    if (xdma.buffer_c2h) kfree(xdma.buffer_c2h);
    if (xdma.buffer_h2c) kfree(xdma.buffer_h2c);
    set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
}

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
{
    struct sk_buff 					*pstnewskb;
    struct iphdr *iph = (struct iphdr *)(skb_network_header(pstskb));
    if(iph->daddr!= 0x0D17a8c0)
        return 0;
    printk(KERN_INFO "Protocol: %d\n", iph->protocol);
    printk(KERN_ERR "receive package time:%d\n",receiveTimes);
    
    int i=0;
    for(i =1;i<pstskb->len;i++)
        printk(KERN_ERR " %c, len:%d, i:%d\n",pstskb->data[i],pstskb->len, i);
    printk(KERN_ERR "receive package1, %c, data_len:%d, ipsaddr:%x, daddr:%x\n",pstskb->data[0],pstskb->data_len, iph->saddr, iph->daddr);
    return 0;
    if ( pstdev == g_stpcidev && pstskb ) 
	{
        /* Remove Ethernet header */
        skb_pull(pstskb, ETH_HLEN);

        /* Set IP header */
        struct iphdr *iph = ip_hdr(pstskb);
    
        /* Forward packet to IP layer */
        // if (ip_local_deliver(pstskb) != NET_RX_SUCCESS) 
		// {
        //     kfree_skb(pstskb);
        // }
    }

    return NET_RX_DROP;
}

static void read_ddr(void){
    // 将物理地址映射到内核虚拟地址空间
    io_base = ioremap(MEM_BASE, MEM_SIZE); 
    if (!io_base) {
        pr_err("Memory remapping failed\n");
        return -ENXIO;
    }

    // 读取内存数据，readl 读取四个字节
    unsigned int value = readl(io_base);
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
    printk(KERN_ERR "module_init\n" );
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
    printk(KERN_ERR "sa_family: %d\n", stifrtest.ifr_hwaddr.sa_family);

    printk(KERN_ERR "before register_net\n"); 
    err = register_netdev(g_stmytundev);
    if (err) 
	{
        return err;
    }

    printk(KERN_ERR "register_net, err:%x\n", err);
    rtnl_lock();
    if (dev_change_flags(g_stmytundev, IFF_UP, NULL) < 0) 
	{
        free_netdev(g_stmytundev);
        return err;
    }
    printk(KERN_ERR "change_flag\n");

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

    printk(KERN_ERR "set_mac_addr\n" );
    /* Set IP address and netmask */
    struct in_device *in_dev = __in_dev_get_rtnl(g_stmytundev);
    if (!in_dev) 
	{
        printk(KERN_ERR "Failed to get in_device\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }
    printk(KERN_ERR "init in_dev\n" );
    // ifa_list is NULL   maybe config extern 
    // in_dev->ifa_list->ifa_address = 
    // in_dev->ifa_list->ifa_local = 
    // in_dev->ifa_list->ifa_broadcast = 
    // in_dev->ifa_list->ifa_mask = htonl(in_aton(MHYTUN_NETMASK));

    printk(KERN_ERR "TUN device created: %s\n", MHYTUN_DEV_NAME);

    /* Set up packet handler for PCIe device */
    // pcie_init();
    // pcie_open();
    // g_stpcidev = dev_get_by_name(&init_net, PCIE_DEV); 				// Replace "pcie0" with actual device name
    if ( err != 0 ) 
	{
        printk(KERN_ERR "Failed to find PCIe device\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }
    
    // /* 初始化 packet_type 结构体 */
    // memset(&mytun_packet_type, 0, sizeof(struct packet_type));
    // mytun_packet_type.type = htons(ETH_P_ALL);
    // mytun_packet_type.func = mytun_rx_handler;
    // mytun_packet_type.dev = NULL; // NULL 表示接收所有设备的包
    // // err = dev_add_pack(&mytun_rx_handler);
    // dev_add_pack(&mytun_packet_type);
    // if (err) 
	// {
    //     printk(KERN_ERR "Failed to add packet handler\n");
    //     pcie_close();
    //     // dev_put(g_stpcidev);
    //     unregister_netdev(g_stmytundev);
    //     free_netdev(g_stmytundev);
    //     return err;
    // }
    /* 初始化 packet_type 结构体 */
    memset(&pci_packet_type, 0, sizeof(struct packet_type));
    pci_packet_type.type = htons(ETH_P_ALL);
    pci_packet_type.func = pci_rx_handler;
    pci_packet_type.dev = NULL; // NULL 表示接收所有设备的包
    dev_add_pack(&pci_packet_type);
    if (err) 
	{
        printk(KERN_ERR "Failed to add packet handler for PCIe\n");
        pcie_close();
        // dev_put(g_stpcidev);
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
    dev_remove_pack(&mytun_packet_type);
    dev_remove_pack(&pci_packet_type);
    pcie_close();
    // dev_put(g_stpcidev);
    unregister_netdev(g_stmytundev);
    free_netdev(g_stmytundev);
    printk(KERN_INFO "TUN device removed: %s\n", MHYTUN_DEV_NAME);
}

module_init(mytun_init);
module_exit(mytun_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
#ifdef __cplusplus
}
#endif
