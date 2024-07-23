# 指定交叉编译工具链的路径
CROSS_COMPILE := /home/nessus/buildroot-2022.02.2/output/host/bin/aarch64-buildroot-linux-uclibc-

# 指定内核源码路径
KERNEL_DIR := /home/nessus/buildroot-2022.02.2/output/build/linux-5.15.18

# 指定要编译的模块文件名（例如：my_module.ko）
obj-m := edu.o

# 编译规则
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) modules
	$(CROSS_COMPILE)gcc -o edu edu_user.c

# 清理规则
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -rf edu

