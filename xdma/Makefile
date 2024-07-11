obj-m := zh2c.o #表示将对应的.c编译成.ko模块
KERNELDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean 

