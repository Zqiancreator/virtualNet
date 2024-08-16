obj-m := irq-drv2.o #表示将对应的.c编译成.ko模块
# KERNELDIR := /lib/modules/$(shell uname -r)/build
KERNELDIR := /home/peta/qian.zhou/peta_prj/intr/petalinux/build/tmp/work-shared/zynqmp-generic/kernel-build-artifacts

# CC  = aarch64-linux-gnu-gcc
ARCH = arm64
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean 

