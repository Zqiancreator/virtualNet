#obj-m := nv_if_h2c.o #表示将对应的.c编译成.ko模块
obj-m += xilinx_axidmatest.o 
#KERNELDIR := /lib/modules/$(shell uname -r)/build
# KERNELDIR := /usr/src/linux-headers-4.18.0-15-generic
 KERNELDIR := /home/npc/peta_prj/522_7ev/petalinux/build/tmp/work-shared/zynqmp-generic/kernel-build-artifacts 

# CC  = aarch64-linux-gnu-gcc
 ARCH = arm64
XDMA_DIR=/home/npc/dma_ip_drivers/XDMA/linux-kernel/xdma/
XDMA_INCLUDE=/home/npc/dma_ip_drivers/XDMA/linux-kernel/include/
EXTRA_CFLAGS := -I$(XDMA_INCLUDE) -I$(XDMA_DIR)
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean 

