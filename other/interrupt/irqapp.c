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
#include <poll.h>
#include <errno.h>
#include <ctype.h>
#include <termios.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAP_SIZE 0x100001  			//映射的内存区大小（一般为一个叶框大小）0x1000 多少个字节
#define MAP_MASK 0xFFFFF 		 	//MAP_MASK = 0XFFF 用于清零低12位，使得偏移量是页面大小的整数倍
#define MAX_LEN  0x40000			// 与MAP_SIZE对应 代表多少个32位（uinsigned int）即一次读取四个字节
#define MAP_OFFST 0x100000
#define MEM_FILE    "/dev/mem"
#define OFFSET_ADDR 0x800000000

#define SERVER "192.168.23.12"  // 目标服务器的IP地址
#define BUFLEN 512  // 最大缓冲区大小
#define PORT 12345   // 目标服务器的端口号

int sock = 0;
char buffer[1024] = {0};


/**
* @brief 直接写入到内存实际的物理地址。
* @details 通过 mmap 映射关系，找到对应的内存实际物理地址对应的虚拟地址，然后写入数据。
* 写入长度，每次最低4字节
* @param[in] writeAddr, unsigned long, 需要操作的物理地址。
* @param[in] buf，unsigned long *, 需要写入的数据。
* @param[in] len，unsigned long, 需要写入的长度，4字节为单位。
* @return ret, int, 如果发送成功，返回0，发送失败，返回-1。
*/
static int Devmem_Write(unsigned long writeAddr, unsigned int* buf, unsigned int len)
{
	int i = 0;
	int ret = 0;
    int fd;
    int offset_len = (writeAddr & MAP_MASK); // 初始偏移量
	void *map_base, *virt_addr; 
	unsigned long addr = writeAddr;
	
	if(len == 0)
	{
        printf("%s %s %d, len = 0\n", __FILE__, __func__, __LINE__);
        return -1;
    }
	
	if ((fd = open(MEM_FILE, O_RDWR | O_SYNC | O_CREAT | O_APPEND)) == -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		return -1;
    }
	
	
	/* Map one page */ //将内核空间映射到用户空间
    map_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr & ~MAP_MASK);
    if(map_base == (void *) -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		close(fd);
		return -1;
    }
	
		// 发送实际数据内容
 	for (i = 0; i < len; i++)
 	{
		// 翻页处理
        if(offset_len >= MAP_MASK)
        {
            offset_len = 0;
            if(munmap(map_base, MAP_SIZE) == -1)
        	{
        		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
        		close(fd);
        		return 0;
        	}
            map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr & ~MAP_MASK);
            if(map_base == (void *) -1)
        	{
        		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
        		close(fd);
        		return 0;
            }
        }
		virt_addr = map_base + (addr & MAP_MASK);	// 映射地址
		// printf("map:%x\n",map_base);
		*((unsigned long *) virt_addr) = buf[i]; 	// 写入数据
		// printf("virt_addr %lx\n",*((unsigned long *) virt_addr));
		addr += 4;
        offset_len += 4;
	}
	    
	if(munmap(map_base, MAP_SIZE) == -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		return -1;
	}
	// lock.l_type = F_UNLCK;
    // fcntl(fd, F_SETLK, &lock);// 解锁
    close(fd);
	// printf("the file was unlocked.\n");
	return 0;
}

static int Devmem_Read(unsigned long readAddr, unsigned int* buf, unsigned long len)
{
	int i = 0;
    int fd,ret;
    int offset_len = (readAddr & MAP_MASK); // 初始偏移量
    void *map_base, *virt_addr; 
	off_t addr = readAddr;
	
	if ((fd = open(MEM_FILE, O_RDWR | O_SYNC)) == -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		return 0;
    }
	
	
    map_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr & ~MAP_MASK);
    if(map_base == (void *) -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		close(fd);
		return 0;
    }
	for (i = 0; i < len; i++)
 	{
		// 翻页处理
        if(offset_len >= MAP_MASK)
        {
            offset_len = 0;
            if(munmap(map_base, MAP_SIZE) == -1)
        	{
        		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
        		close(fd);
        		return 0;
        	}
            map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr & ~MAP_MASK);
            if(map_base == (void *) -1)
        	{
        		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
        		close(fd);
        		return 0;
            }
        }
		virt_addr = map_base + (addr & MAP_MASK);	// 将内核空间映射到用户空间操作
		// printf("R map:%x\n",map_base);
		buf[i] = *((unsigned long *) virt_addr);	// 读取数据
		// printf("R virt_addr %lx\n",*((unsigned long *) virt_addr));
 		addr += 4;
        offset_len += 4;
	}
	if(munmap(map_base, MAP_SIZE) == -1)
	{
		fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno));
		close(fd);
		return 0;
	}
    
	close(fd);
	return i;
}

// 信号处理函数
void my_signal_fun(int signum)
{
	printf("sigal number = %d\n", signum);
	Devmem_Read(OFFSET_ADDR,buffer,1024);
	send(sock,buffer,4,0);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 将IPv4地址从文本转换为二进制形式
    if(inet_pton(AF_INET, SERVER, &serv_addr.sin_addr)<=0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 连接到服务器上的监听端口
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }
	// close(sock);

	unsigned char key_val;
	int ret;
	int Oflags;

	// 在应用程序中捕捉SIGIO信号（由驱动程序发送）后到信号处理函数
	signal(SIGIO, my_signal_fun);
	int fd = open("/dev/irq_drv", O_RDWR);
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
