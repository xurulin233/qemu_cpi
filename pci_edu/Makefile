
#KERNELDIR ?= /lib/modules/$(shell uname -r)/build  
KERNELDIR ?= /home/nessus/buildroot-2022.02.2/output/build/linux-5.15.18
MODULE_NAME = pci_edu

PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules 

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules clean

.PHONY: all clean

obj-m := $(MODULE_NAME).o

