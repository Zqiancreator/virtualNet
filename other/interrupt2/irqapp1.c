#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <fcntl.h>

// 信号处理函数
void my_signal_fun(int signum)
{
	printf("sigal number = %d\n", signum);
}

int main(int argc, char *argv[])
{
	unsigned char key_val;
	int ret;
	int Oflags;

	// 在应用程序中捕捉SIGIO信号（由驱动程序发送）后到信号处理函数
	signal(SIGIO, my_signal_fun);
	int fd = open("/dev/irq_drv1", O_RDWR);
	if (fd < 0)
	{
		printf(">>can't open file!\n");
	}
    // fd设备文件句柄  F_SETOWN表示设置将接收SIGIO或SIGURG信号的线程ID  getpid()获取当前线程ID
	fcntl(fd, F_SETOWN, getpid());
    // 获取当前进程状态
	Oflags = fcntl(fd, F_GETFL); 
    // 开启当前线程异步通知
	fcntl(fd, F_SETFL, Oflags | FASYNC);
	while (1)
	{
		usleep(1000);
	}
	return 0;
}
