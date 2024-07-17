// // 定义一个通知链
// int intrFinish=1;
// EXPORT_SYMBOL(intrFinish);

// 	static RAW_NOTIFIER_HEAD(my_notifier_chain);

// 	// 发送通知的函数
// 	static void send_notification(void)
// 	{
// 		int ret;
// 		// 向通知链的所有注册者发送通知
//         char* val = "Hello, nvif\n";
// 		ret = raw_notifier_call_chain(&my_notifier_chain, 0, val);
// 		printk(KERN_INFO "Notification sent, return value: %d\n", ret);
// 	}
// // 供其他模块注册到这个通知链的接口函数
// 	int register_my_notifier(struct notifier_block *nb)
// 	{
// 		return raw_notifier_chain_register(&my_notifier_chain, nb);
// 	}
// 	EXPORT_SYMBOL(register_my_notifier);

// 	// 供其他模块从这个通知链注销的接口函数
// 	int unregister_my_notifier(struct notifier_block *nb)
// 	{
// 		return raw_notifier_chain_unregister(&my_notifier_chain, nb);
// 	}
// 	EXPORT_SYMBOL(unregister_my_notifier);
