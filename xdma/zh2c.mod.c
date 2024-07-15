#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xdd8f8694, "module_layout" },
	{ 0x87d7b3f5, "kthread_stop" },
	{ 0x2b68bd2f, "del_timer" },
	{ 0x6ea0695, "dev_remove_pack" },
	{ 0xab36bffd, "dev_add_pack" },
	{ 0x6bb70076, "wake_up_process" },
	{ 0xa6521794, "kthread_create_on_node" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0xb099df7a, "pci_get_device" },
	{ 0x32ec28ed, "free_netdev" },
	{ 0x754f3d58, "unregister_netdev" },
	{ 0x6e720ff2, "rtnl_unlock" },
	{ 0xe73fce7a, "dev_open" },
	{ 0x3b6a91ed, "dev_set_mac_address" },
	{ 0xc9057acf, "dev_close" },
	{ 0x3a16c210, "dev_change_flags" },
	{ 0xc7a4fbed, "rtnl_lock" },
	{ 0x9e61fe6d, "register_netdev" },
	{ 0xc28f897e, "alloc_netdev_mqs" },
	{ 0x547a10d9, "ether_setup" },
	{ 0xcbf3f793, "register_my_notifier" },
	{ 0x5e5292c, "filp_close" },
	{ 0xddd346a3, "filp_open" },
	{ 0x56b1771b, "current_task" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0x31e2d6ad, "dma_direct_unmap_sg" },
	{ 0x20d8bf1e, "dma_direct_map_sg" },
	{ 0x74c9b7b5, "dma_ops" },
	{ 0xe6db25e6, "my_xdma_xfer_submit" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xf888ca21, "sg_init_table" },
	{ 0x37a0cba, "kfree" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x410ee47e, "eth_mac_addr" },
	{ 0x7e736549, "kfree_skb" },
	{ 0xf95117c8, "netif_rx" },
	{ 0x8cc57f6f, "eth_type_trans" },
	{ 0x1ed8b599, "__x86_indirect_thunk_r8" },
	{ 0x559069b0, "skb_put" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x71a7b444, "__alloc_skb" },
	{ 0xa3bfbf9e, "dev_get_by_name" },
	{ 0x30cb0399, "init_net" },
	{ 0x3eeb2322, "__wake_up" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x1000e51, "schedule" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x4e1bcc1b, "kernel_write" },
	{ 0xca7a3159, "kmem_cache_alloc_trace" },
	{ 0x428db41d, "kmalloc_caches" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "xdma");


MODULE_INFO(srcversion, "F5D943FD1A102ABA0F40D5C");
