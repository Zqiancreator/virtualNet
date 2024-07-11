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
#include <linux/cdev.h>
#include <linux/device.h>
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
#define PCIE_DEV_H2C                "/dev/xdma0_h2c_0"
#define PCIE_DEV_C2H                "/dev/xdma0_c2h_0"
#define H2C_OFFSET              0x800000000
#define C2H_OFFSET              0x800000000
/*******************************************************************************
                              Type definitions                                
 ******************************************************************************/
unsigned char fpga_mac_src[] = FPGATUN_MAC_ADDR;
unsigned char mhy_mac_src[] = MHYTUN_MAC_ADDR;
static struct packet_type mytun_packet_type;
static struct packet_type pci_packet_type;
static int  receiveTimes;
/*******************************************************************************
                         	  Local function declarations                          
 ******************************************************************************/
int pci_send(char *data, size_t len);
int pcie_init(void);
int pcie_open(void);
void pcie_close(void);
int read_pcie(void *pci_buff, size_t len);
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
                    struct sk_buff *skb：指向接收到的数据包的Socket缓冲区结构的指针。
                    struct net_device *dev：指向接收数据包的网络设备结构的指针。
                    struct packet_type *pt：指向数据包类型结构的指针，这通常是您传递给dev_add_pack的结构。
                    struct net_device *orig_dev：原始接收设备，通常用于高级用途，如网桥处理。
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
{// Ethernet and MAC is not necessary?
    // struct ethhdr 					*psteth;
    // void 							*ppcidata;
    // size_t							 pci_data_len;

    struct iphdr *iph = (struct iphdr *)(skb_network_header(pstskb));
    if(iph->daddr!= 0x0c17a8c0)
        return 0;
    printk(KERN_INFO "Protocol: %d\n", iph->protocol);
    printk(KERN_ERR "receive package time:%d\n",receiveTimes);
    if(receiveTimes<=655355)
        receiveTimes++ ;
    int i=0;
    for(i =1;i<pstskb->len;i++){
        printk(KERN_ERR " %c, len:%d, i:%d\n",pstskb->data[i],pstskb->len, i);
    }
    pci_send(pstskb->data,pstskb->len);
    printk(KERN_ERR "receive package1, %c, data_len:%d, ipsaddr:%x, daddr:%x\n",pstskb->data[0],pstskb->data_len, iph->saddr, iph->daddr);
    return 0;
    // if ((pstdev == g_stmytundev) && pstskb) 
	// {
    //     printk(KERN_ERR "receive package time:%d\n",receiveTimes);
    //     if(receiveTimes<=655355)
    //     receiveTimes++ ;
    //     printk(KERN_ERR "receive package2, %s\n",pstskb->data);
    //     return 0;
    //     /* Add Ethernet header */
    //     skb_push(pstskb, ETH_HLEN);

    //     /* Set MAC header */
    //     psteth = (struct ethhdr *)pstskb->data;
    //     memcpy(psteth->h_source, mhy_mac_src, ETH_ALEN);
		
    //     /* 设置目标 MAC 地址 */
    //     memcpy(psteth->h_dest, fpga_mac_src, ETH_ALEN);
    //     psteth->h_proto = htons(ETH_P_IP);

    //     /* 准备将数据发送到 PCIe 设备 */
    //     ppcidata 		= pstskb->data;
    //     pci_data_len 	= pstskb->len;

    //     /* 调用函数将数据发送到 PCIe 设备 */
    //     if (pci_send(ppcidata, pci_data_len) != 0)
	// 	{
	// 		/* 发送失败时释放 skb */
    //         kfree_skb(pstskb); 
    //         return NET_RX_DROP;
    //     }

	// 	/* 成功发送后释放 skb */
    //     kfree_skb(pstskb); 
    //     return NET_RX_SUCCESS;
    // }

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
// int pci_send(char *data, size_t len)
// {
//     printk(KERN_ERR "len:%d\n",len);
//     char *pci_buffer = kmalloc(len, GFP_ATOMIC);
//     if(!pci_buffer){
//         printk(KERN_ERR "kmalloc fail\n");
//     }else{
//         printk(KERN_ERR "kmalloc success\n");
        
//     }
//     if (!pci_buffer) 
// 	{
//         printk(KERN_ERR "Failed to allocate PCIe buffer\n");
//         return -ENOMEM;
//     }

//     memcpy(pci_buffer, data, len);

//     // /* 假设 pcie_send_buffer 是实际发送数据到 PCIe 的函数 */

//     // int i=0;
//     // for(i=0;i<len;i++){
//     //     printk(KERN_ERR "buffer wait for sending :%c\n", pci_buffer[i]);
//     // }
//     // ssize_t ret = g_stpcidev.h2c0->f_op->write(g_stpcidev.h2c0, pci_buffer,len,&g_stpcidev.h2c0->f_pos); 
//     mm_segment_t old_fs;

//     old_fs = get_fs();
//     set_fs(KERNEL_DS);
//     ssize_t ret = g_stpcidev.h2c0->f_op->write(g_stpcidev.h2c0, pci_buffer,len,&g_stpcidev.h2c0->f_pos); 
//     if(ret <= 0){
//         printk(KERN_ERR "write error ret1:%x\n",ret);
//     }else
//         printk(KERN_ERR "write success ret1:%x\n",ret);
//     ret = kernel_write(g_stpcidev.h2c0, pci_buffer, len, &g_stpcidev.h2c0->f_pos);
//     if(ret <= 0){
//         printk(KERN_ERR "write error ret2:%x\n",ret);
//     }else
//         printk(KERN_ERR "write success ret2:%x\n",ret);
//     set_fs(old_fs);

//     // if(ret <= 0){
//     //     printk(KERN_ERR "write error ret3:%x\n",ret);
//     // }else
//     //     printk(KERN_ERR "write success ret:%x\n",ret);
//     ret = read_pcie(pci_buffer,len);
//     if(ret <= 0){
//         printk(KERN_ERR "read error ret:%x\n",ret);
//     }else
//         printk(KERN_ERR "read success ret:%x\n",ret);
// 	// /* 释放 PCIe 缓冲区 */
//     kfree(pci_buffer);
//     return ret;
// }
int pci_send(char *data, size_t len) {
    dma_addr_t dma_handle;
    void *pci_buffer;
    ssize_t ret;
    mm_segment_t old_fs;
    // 分配内核缓冲区
    pci_buffer = kmalloc(len, GFP_KERNEL);
    if (!pci_buffer) {
        printk(KERN_ERR "Failed to allocate PCIe buffer\n");
        return -ENOMEM;
    }
    if (!access_ok(data, len)) {
        printk(KERN_ERR "Invalid user space pointer\n");
        kfree(pci_buffer);
        filp_close(g_stpcidev.h2c0, NULL);
        return -EFAULT;
    }
    // 复制数据到内核缓冲区
    if (copy_from_user(pci_buffer, data, len)) {
        printk(KERN_ERR "Failed to copy data from user space\n");
        kfree(pci_buffer);
        filp_close(g_stpcidev.h2c0, NULL);
        return -EFAULT;
    }
    // 映射内存到设备地址空间
//     if (g_stpcidev.h2c0->f_inode->i_cdev && g_stpcidev.h2c0->f_inode->i_cdev->dev && pci_buffer) {
//         printk(KERN_ERR "g_stpcidev.h2c0->f_inode->i_cdev is not null\n");
//         dma_handle = dma_map_single(g_stpcidev.h2c0->f_inode->i_cdev->dev, pci_buffer, len, DMA_TO_DEVICE);
//     }
//     else {

//         printk(KERN_ERR "g_stpcidev.h2c0->f_inode->i_cdev is null\n");
//     }
// return 0;
//     if (dma_mapping_error(NULL, dma_handle)) {
//         printk(KERN_ERR "Failed to map DMA buffer\n");
//         kfree(pci_buffer);
//         return -EFAULT;
//     }

    // 更改地址空间
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    // 使用设备驱动程序的写操作
    if (g_stpcidev.h2c0->f_inode->i_cdev && g_stpcidev.h2c0->f_inode->i_cdev->dev && pci_buffer){
        ret = g_stpcidev.h2c0->f_op->write(g_stpcidev.h2c0, pci_buffer, len, &g_stpcidev.h2c0->f_pos);
        
        if (ret <= 0) {
            printk(KERN_ERR "write error ret1: %zx\n", ret);
        } else {
            printk(KERN_INFO "write success ret1: %zx\n", ret);
        }
    }

    // 使用 kernel_write 进行写操作
    ret = kernel_write(g_stpcidev.h2c0, pci_buffer, len, &g_stpcidev.h2c0->f_pos);
    if (ret <= 0) {
        printk(KERN_ERR "write error ret2: %zx\n", ret);
    } else {
        printk(KERN_INFO "write success ret2: %zx\n", ret);
    }
    read_pcie(pci_buffer, len);
    if (ret <= 0) {
        printk(KERN_ERR "read error ret2: %zx\n", ret);
    } else {
        printk(KERN_INFO "read success ret2: %zx\n", ret);
    }
    // 恢复地址空间
    set_fs(old_fs);

    // 取消映射内存
    // dma_unmap_single(NULL, dma_handle, len, DMA_TO_DEVICE);

    // 释放内核缓冲区
    kfree(pci_buffer);

    return ret; 
}
int read_pcie(void *pci_buff, size_t len){
    int ret = g_stpcidev.c2h0->f_op->read(g_stpcidev.c2h0,pci_buff,len,&g_stpcidev.c2h0->f_pos); 

    // struct sk_buff *pcieskb;
    // if (skb_tailroom(pcieskb) >= len) {
    //     unsigned char *skb_data;

    // // 使用 skb_put 增加skb数据部分的长度，并获得新增部分的指针
    //     skb_data = skb_put(pcieskb, len);

    //     // 将你的数据复制到 skb_data 指向的位置
    //     memcpy(skb_data, pci_buff, len);
    // } else {
    //     // 处理空间不足的情况
    //     printk(KERN_ERR, "not enough space\n");
    // }
    // skb_pull(pcieskb, 14);// 去除以太网头部，14为标准长度   
    return ret;
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
    mm_segment_t old_fs;

    old_fs = get_fs();
    set_fs(KERNEL_DS);

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
    set_fs(old_fs);

    return status;

cleanup_handles:
    if (!IS_ERR(g_stpcidev.c2h0) && g_stpcidev.c2h0)
        filp_close(g_stpcidev.c2h0, NULL);
    if (!IS_ERR(g_stpcidev.h2c0) && g_stpcidev.h2c0)
        filp_close(g_stpcidev.h2c0, NULL);
    set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
    return status;

}

//关闭PCIe设备
void pcie_close(void)
{
    mm_segment_t old_fs;

    old_fs = get_fs();
    set_fs(KERNEL_DS);
	if (g_stpcidev.c2h0) filp_close(g_stpcidev.c2h0,NULL);
    if (g_stpcidev.h2c0) filp_close(g_stpcidev.h2c0,NULL);
    if (g_stpcidev.buffer_c2h) kfree(g_stpcidev.buffer_c2h);
    if (g_stpcidev.buffer_h2c) kfree(g_stpcidev.buffer_h2c);
    set_fs(old_fs); // 确保在任何出口点恢复原始的地址空间
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
    struct in_ifaddr *ifa;
    struct in_device *in_dev = __in_dev_get_rtnl(g_stmytundev);
    if (!in_dev) 
	{
        printk(KERN_ERR "Failed to get in_device\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }
    printk(KERN_ERR "init in_dev\n" );
    // 分配新的接口地址结构
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
    else 
        ifa->ifa_next = in_dev->ifa_list;
    printk(KERN_ERR " ifa_list = ifa\n");
    printk(KERN_INFO "Adding ifa to in_dev->ifa_list. in_dev: %p, current ifa_list: %p\n", in_dev, in_dev->ifa_list);
    // in_dev->ifa_list = ifa;
    printk(KERN_INFO "Added. in_dev: %p, new ifa_list: %p\n", in_dev, in_dev->ifa_list);
    printk(KERN_ERR "add ifa_list\n");
    // in_dev->ifa_list->ifa_address = 
    // in_dev->ifa_list->ifa_local = 
    // in_dev->ifa_list->ifa_broadcast = 
    // in_dev->ifa_list->ifa_mask = htonl(in_aton(MHYTUN_NETMASK));

    printk(KERN_ERR "TUN device created: %s\n", MHYTUN_DEV_NAME);
    /* Set up packet handler for PCIe device */
    err = pcie_init();
    printk(KERN_ERR "pcie init\n");
    err = pcie_open();
    printk(KERN_ERR "pcie open\n");
    if (err != 0) 
	{
        printk(KERN_ERR "Failed to find PCIe device\n");
        unregister_netdev(g_stmytundev);
        free_netdev(g_stmytundev);
        return err;
    }
    printk(KERN_ERR "before add pack\n");
    /* 初始化 packet_type 结构体 */
    memset(&mytun_packet_type, 0, sizeof(struct packet_type));
    mytun_packet_type.type = htons(ETH_P_IP);//只接收IP数据包
    // mytun_packet_type.type = htons(ETH_P_ALL);//接收所有数据包
    mytun_packet_type.func = mytun_rx_handler;
    mytun_packet_type.dev = NULL; // NULL 表示接收所有设备的包
    dev_add_pack(&mytun_packet_type);
    printk(KERN_ERR " add pack success\n");
    // while(read_pcie){// constantly read from ddr

    // }
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
