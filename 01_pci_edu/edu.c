#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "edu.h"

#define EDU_PRINTK(level, format, arg...) printk(level "[Kernel: %s - %d] " format, __FUNCTION__, __LINE__, ##arg)
#define EDU_ERR(format, arg...)           EDU_PRINTK(KERN_ERR, format, ##arg)
#define EDU_INFO(format, arg...)          EDU_PRINTK(KERN_INFO, format, ##arg)

/* 定义 PCI 设备 ID */
#define EDU_DEVICE_VENDOR_ID 0x1234 /* Vendor ID */
#define EDU_DEVICE_DEVICE_ID 0x11e8 /* Device ID */

/* 定义 character device 的名称以及类名 */
#define EDU_DEVICE_NAME "edu"
#define EDU_CLASS_NAME  "edu"

/* EDU 寄存器读写操作 */
#define EDU_READ_REG(addr)        readl(g_edu_dev->ioaddr + (addr))
#define EDU_WRITE_REG(addr, data) writel((data), g_edu_dev->ioaddr + (addr))

struct edu_ioctl {
    uint64_t start; /* pci bar0起始地址 */
    uint64_t end;   /* pci bar0结束地址 */
    uint64_t len;   /* pci bar0空间大小 */
};

/* EDU设备管理结构体 */
struct edu_pci_dev {
    struct pci_dev *dev;
    void __iomem   *ioaddr; /* 映射后的地址 */
    uint64_t        start;  /* pci bar0起始地址 */
    uint64_t        end;    /* pci bar0结束地址 */
    uint64_t        len;    /* pci bar0空间大小 */
    int             irq;    /* edu设备中断号 */

    /* character device 所需的内容 */
    int            chr_major;
    struct class  *chr_class;
    struct device *chr_device;

    wait_queue_head_t irq_wq;      /* 等待队列，用于阻塞EDU_WAIT_IRQ请求 */
    atomic_t          irq_handled; /* 用于阻塞用户态中断请求，作为条件变量存在 */
};

/* 创建一个字符设备, 和用户空间进行通讯 */
static int  edu_mmap(struct file *filp, struct vm_area_struct *vma);
static long edu_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static long edu_compat_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static struct file_operations g_edu_fops = {
    .owner = THIS_MODULE,
    .mmap = edu_mmap,
    .unlocked_ioctl = edu_ioctl,
    .compat_ioctl = edu_compat_ioctl,
};

static struct edu_pci_dev *g_edu_dev;

// 定义 PCI 设备表
static const struct pci_device_id edu_table[] = {
    {PCI_DEVICE(EDU_DEVICE_VENDOR_ID, EDU_DEVICE_DEVICE_ID)},
    {0},
};

// 定义 PCI 驱动
static int edu_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void edu_remove(struct pci_dev *dev);

static struct pci_driver g_edu_driver = {
    .name = "edu",
    .id_table = edu_table,
    .probe = edu_probe,
    .remove = edu_remove,
};

static int edu_mmap(struct file *filp, struct vm_area_struct *vma)
{
    size_t      size = vma->vm_end - vma->vm_start;
    phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

    if ((offset >> PAGE_SHIFT) != vma->vm_pgoff) {
        return -EINVAL;
    }

    if ((offset + (phys_addr_t)size - 1) < offset) {
        return -EINVAL;
    }

    if (!pfn_valid(vma->vm_pgoff)) {
        vma->vm_page_prot = phys_mem_access_prot(filp, vma->vm_pgoff, size, vma->vm_page_prot);
    }

    if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size, vma->vm_page_prot) != 0) {
        return -EAGAIN;
    }

    return 0;
}

static long edu_compat_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    return edu_ioctl(f, cmd, (unsigned long)((void __user *)(unsigned long)(arg)));
}

static long edu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct edu_ioctl ioctl;

    switch (cmd) {
        case EDU_WAIT_IRQ:
            EDU_INFO("Edu live reg 0x%x\n", EDU_READ_REG(IO_DEV_CARD_LIVENESS));
            EDU_INFO("Edu ioctl wait irq!\n");
            wait_event_interruptible(g_edu_dev->irq_wq, atomic_read(&g_edu_dev->irq_handled) != 0);
            atomic_set(&g_edu_dev->irq_handled, 0);
            break;
        case EDU_ENABLE_IRQ:
            EDU_INFO("Edu ioctl enable irq!\n");
            EDU_WRITE_REG(IO_DEV_STATUS, 0x80);
            EDU_INFO("Edu ioctl: IO_DEV_STATUS 0x%x\n", EDU_READ_REG(IO_DEV_STATUS));
            break;
        case EDU_GET_BAR_INFO:
            EDU_INFO("Edu get bar information!\n");
            ioctl.start = g_edu_dev->start;
            ioctl.end = g_edu_dev->end;
            ioctl.len = g_edu_dev->len;
            break;
        default:
            return -EINVAL;
    }

    if (copy_to_user((void *)arg, &ioctl, sizeof(struct edu_ioctl))) {
        return -1;
    }

    return 0;
}

// 定义中断处理函数
static irqreturn_t edu_irq_handler(int irq, void *dev)
{
    uint32_t value = 0;
    uint32_t irq_status = 0;

    /* 关闭中断 */
    EDU_WRITE_REG(IO_DEV_STATUS, 0x0);
    EDU_INFO("edu: IO_DEV_STATUS 0x%x\n", EDU_READ_REG(IO_DEV_STATUS));

    /* 读取中断状态 */
    irq_status = EDU_READ_REG(IO_DEV_IRQ_STATUS);
    EDU_INFO("edu: IRQ %d triggered, %d\n", irq, irq_status);

    /* 将中断状态写入中断确认寄存器，清除中断
     * TODO? 不清楚为什么不能放到用户态执行 (现象: 放到用户态会一直上报中断)
    */
    EDU_WRITE_REG(IO_DEV_IRQ_ACK, irq_status);

    /* 读取阶乘结果 */
    value = EDU_READ_REG(IO_DEV_VALUE);
    EDU_INFO("edu: value read from device: 0x%x\n", value);

    /* 唤醒用户态中断处理线程 */
    atomic_set(&g_edu_dev->irq_handled, 1);
    wake_up_interruptible(&g_edu_dev->irq_wq);

    return IRQ_HANDLED;
}

// 定义 PCI 设备的 probe 函数
static int edu_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int retval = 0;

    EDU_INFO("Irq num: %d\n", dev->irq);
    EDU_INFO("Vendor id: 0x%x\n", dev->vendor);
    EDU_INFO("Device id: 0x%x\n", dev->device);

    // 首先打开设备
    if (pci_enable_device(dev)) {
        EDU_ERR("edu: Cannot enable PCI device\n");
        retval = -EIO;
        goto out_edu_all;
    }

    // 复制设备的中断号到结构体并检查
    g_edu_dev->irq = dev->irq;
    if (g_edu_dev->irq < 0) {
        EDU_ERR("edu: Invalid IRQ number\n");
        goto out_edu_all;
    }

    // 请求设备的内存区域
    retval = pci_request_regions(dev, "edu");
    if (retval) {
        EDU_ERR("edu: Cannot request regions\n");
        goto out_edu_all;
    }

    g_edu_dev->start = pci_resource_start(dev, 0);
    g_edu_dev->end = pci_resource_end(dev, 0);
    g_edu_dev->len = pci_resource_len(dev, 0);
    EDU_INFO("Bar0 address start: 0x%llx\n", g_edu_dev->start);
    EDU_INFO("Bar0 address   end: 0x%llx\n", g_edu_dev->end);
    EDU_INFO("Bar0 address  size: 0x%llx\n", g_edu_dev->len);

    // 映射设备的内存区域
    g_edu_dev->ioaddr = pci_ioremap_bar(dev, 0);
    if (!g_edu_dev->ioaddr) {
        EDU_ERR("edu: Cannot map device memory\n");
        retval = -ENOMEM;
        goto out_regions;
    }
    EDU_INFO("Bar0 ioaddr: 0x%px\n", g_edu_dev->ioaddr);

    // 设置中断处理函数
    retval = request_irq(dev->irq, edu_irq_handler, IRQF_SHARED, "edu", g_edu_dev);
    if (retval) {
        EDU_ERR("edu: Cannot set up IRQ handler\n");
        goto out_ioremap;
    }
    // 启用设备的中断发起
    EDU_WRITE_REG(IO_DEV_STATUS, 0x80);

    g_edu_dev->dev = dev;

    return 0;

out_ioremap:
    pci_iounmap(dev, g_edu_dev->ioaddr);
out_regions:
    pci_release_regions(dev);
out_edu_all:
    return retval;
}

// 定义 PCI 设备的 remove 函数
static void edu_remove(struct pci_dev *dev)
{
    // 释放中断
    free_irq(g_edu_dev->irq, g_edu_dev);

    // 释放内存区域
    pci_iounmap(dev, g_edu_dev->ioaddr);
    pci_release_regions(dev);

    // 停用设备
    pci_disable_device(dev);
    
    EDU_INFO("edu remove done!\n");
}

// 驱动的初始化函数与卸载函数
static int __init edu_init(void)
{
    int32_t retval = 0;

    /* 为EDU设备结构体分配内存 */
    g_edu_dev = kmalloc(sizeof(struct edu_pci_dev), GFP_KERNEL);
    if (!g_edu_dev) {
        EDU_ERR("edu: Cannot allocate memory for the device\n");
        return -ENOMEM;
    }

    /* 先注册一个字符设备 */
    g_edu_dev->chr_major = register_chrdev(0, EDU_DEVICE_NAME, &g_edu_fops);
    if (g_edu_dev->chr_major < 0) {
        EDU_ERR("edu: Cannot register char device\n");
        goto out_edu_all;
    }
    g_edu_dev->chr_class = class_create(THIS_MODULE, EDU_CLASS_NAME);
    if (IS_ERR(g_edu_dev->chr_class)) {
        EDU_ERR("edu: Cannot create class\n");
        goto out_edu_chr_device;
    }
    g_edu_dev->chr_device = device_create(g_edu_dev->chr_class, NULL, MKDEV(g_edu_dev->chr_major, 0), NULL, EDU_DEVICE_NAME);
    if (IS_ERR(g_edu_dev->chr_device)) {
        EDU_ERR("edu: Cannot create device\n");
        goto out_edu_class;
    }

    /* pci device register */
    retval = pci_register_driver(&g_edu_driver);
    if (retval) {
        EDU_ERR("pci_register_driver fail!\n");
        goto out_pci_register;
    }

    /* 初始化等待队列 */
    init_waitqueue_head(&g_edu_dev->irq_wq);
    atomic_set(&g_edu_dev->irq_handled, 0);

    return retval;

out_pci_register:
    device_destroy(g_edu_dev->chr_class, MKDEV(g_edu_dev->chr_major, 0));
out_edu_class:
    class_destroy(g_edu_dev->chr_class);
out_edu_chr_device:
    unregister_chrdev(g_edu_dev->chr_major, EDU_DEVICE_NAME);
out_edu_all:
    kfree(g_edu_dev);
    return retval;
}

static void __exit edu_exit(void)
{
    /* 字符设备注销 */
    device_destroy(g_edu_dev->chr_class, MKDEV(g_edu_dev->chr_major, 0));
    class_destroy(g_edu_dev->chr_class);
    unregister_chrdev(g_edu_dev->chr_major, EDU_DEVICE_NAME);

    /* pci注销 */
    pci_unregister_driver(&g_edu_driver);

    kfree(g_edu_dev);
    EDU_INFO("edu ko exit done!\n");
}

// 注册驱动的初始化函数与卸载函数
module_init(edu_init);
module_exit(edu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MrLayfolk");
MODULE_DESCRIPTION("edu driver");
MODULE_VERSION("1.0.0");

