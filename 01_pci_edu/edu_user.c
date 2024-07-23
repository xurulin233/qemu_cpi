#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "edu.h"

static int   g_edu_filep = -1;
static void *g_edu_bar_vaddr = NULL; /* Bar空间虚拟地址 */

struct edu_ioctl {
    uint64_t start; /* pci bar0起始地址 */
    uint64_t end;   /* pci bar0结束地址 */
    uint64_t len;   /* pci bar0空间大小 */
};

int edu_read_reg(uint32_t addr)
{
    volatile uint32_t *vaddr32 = NULL;
    vaddr32 = (uint32_t *)((uint8_t *)g_edu_bar_vaddr + addr);
    return *vaddr32;
}

void edu_write_reg(uint32_t addr, uint32_t data)
{
    volatile uint32_t *vaddr32 = NULL;
    vaddr32 = (uint32_t *)((uint8_t *)g_edu_bar_vaddr + addr);
    *vaddr32 = data;
}

void print_edu_regs(void)
{
    printf("IO_DEV_CARD_ID       (0x00) = 0x%x\n", edu_read_reg(IO_DEV_CARD_ID));
    printf("IO_DEV_CARD_LIVENESS (0x04) = 0x%x\n", edu_read_reg(IO_DEV_CARD_LIVENESS));
    printf("IO_DEV_VALUE         (0x08) = 0x%x\n", edu_read_reg(IO_DEV_VALUE));
    printf("IO_DEV_STATUS        (0x20) = 0x%x\n", edu_read_reg(IO_DEV_STATUS));
    printf("IO_DEV_IRQ_STATUS    (0x24) = 0x%x\n", edu_read_reg(IO_DEV_IRQ_STATUS));
    printf("IO_DEV_IRQ_ACK       (0x64) = 0x%x\n", edu_read_reg(IO_DEV_IRQ_ACK));
}

void *edu_mmap(uint32_t size, off_t target, uint64_t *v_addr)
{
    void    *map_base = NULL;
    void    *virt_addr = NULL;
    unsigned page_size = 0;
    unsigned mapped_size = 0;
    unsigned offset_in_page = 0;

    mapped_size = page_size = getpagesize();
    offset_in_page = (unsigned)target & (page_size - 1);
    if (offset_in_page + size > page_size) {
        mapped_size = ((offset_in_page + size) / page_size) * page_size;
        if ((offset_in_page + size) % page_size) {
            mapped_size += page_size;
        }
    }
    map_base = mmap(NULL, mapped_size, (PROT_READ | PROT_WRITE), MAP_SHARED,
                    g_edu_filep, target & ~(off_t)(page_size - 1));
    if (map_base == MAP_FAILED) {
        printf("mmap target addr 0x%x failed.\n", target);
        return MAP_FAILED;
    }
    virt_addr = (char *)map_base + offset_in_page;
    return virt_addr;
}

void edu_irq_handler(void)
{
    printf("irq handler in userspace start ... \n");
    print_edu_regs();
    printf("irq handler in userspace done ... \n");
}

void *edu_irq_thread_fn(void *arg)
{
    printf("Thread Running\n");
    /* 内核态:
     * 1. 使能中断, 等待中断产生;
     * 2. 中断产生后, 关闭中断, 清除中断, 唤醒用户态中断线程;
     * 用户态:
     * 1. 初始化请求中断, 等待中断产生;
     * 2. 中断产生后, 进行相关处理, 然后使能中断
     */
    while (1) {
        /* 等待中断 */
        ioctl(g_edu_filep, EDU_WAIT_IRQ);

        /* 中断处理函数 */
        edu_irq_handler();

        /* 使能中断 */
        ioctl(g_edu_filep, EDU_ENABLE_IRQ);
    }
}

int main(int argc, char **argv)
{
    int              ret = 0;
    struct edu_ioctl edu_ioctl = {0};
    pthread_t        thread_id;

    g_edu_filep = open("/dev/edu", O_RDWR);
    if (g_edu_filep < 0) {
        printf("open %s failed!\n", "/dev/edu");
    }

    /* 获取pci bar信息 */
    ioctl(g_edu_filep, EDU_GET_BAR_INFO, &edu_ioctl);
    printf("Bar0 address start: 0x%llx\n", edu_ioctl.start);
    printf("Bar0 address   end: 0x%llx\n", edu_ioctl.end);
    printf("Bar0 address  size: 0x%llx\n", edu_ioctl.len);
    g_edu_bar_vaddr = edu_mmap(edu_ioctl.len, (off_t)edu_ioctl.start, NULL);
    printf("Bar0 vaddr: 0x%p\n", g_edu_bar_vaddr);

    /* 寄存器读写测试 */
    print_edu_regs();
    edu_write_reg(IO_DEV_CARD_LIVENESS, 0x2);
    print_edu_regs();

    /* 创建中断处理线程 */
    if (pthread_create(&thread_id, NULL, edu_irq_thread_fn, NULL) != 0) {
        printf("Failed to create thread\n");
        return 1;
    }
    /* 等待线程结束 */
    pthread_join(thread_id, NULL);

    return 0;
}

