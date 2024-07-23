#ifndef EDU_H
#define EDU_H

#include <linux/ioctl.h>

/* 定义要用到的寄存器偏移量:
- 0x04 (RW)：卡活体检查，对写入的值取反，并返回
- 0x08 (RW)：因子计算，写入一个数值，返回该数值的阶乘
- 0x20 (RW)：状态寄存器，其低1位为只读，记录设备是否在进行阶乘操作，完成则为0，否则为1；
             其从低向高第8位为读写，记录设备是否在完成阶乘操作后发起中断，发起则为1，否则为0。
- 0x24 (RO)：中断状态寄存器，记录中断状态，0x00000001为阶乘中断，0x00000100为 DMA 中断。
- 0x60 (WO)：触发中断寄存器，引发中断，中断状态将放入中断状态寄存器（使用按位 OR）。
- 0x64 (WO)：用于清除中断，将0x24寄存器清零并阻止设备继续发出中断。
*/
#define IO_DEV_CARD_ID       0x00
#define IO_DEV_CARD_LIVENESS 0x04
#define IO_DEV_VALUE         0x08
#define IO_DEV_STATUS        0x20
#define IO_DEV_IRQ_STATUS    0x24
#define IO_DEV_IRQ_ACK       0x64

/* 定义 ioctl 的操作号 */
#define EDU_IOCTL_CMD_MAGIC 'e'
#define EDU_WAIT_IRQ        _IO(EDU_IOCTL_CMD_MAGIC, 1) /* 等待EDU中断 */
#define EDU_ENABLE_IRQ      _IO(EDU_IOCTL_CMD_MAGIC, 2) /* 使能EDU中断 */
#define EDU_GET_BAR_INFO    _IO(EDU_IOCTL_CMD_MAGIC, 3) /* 获取EDU Bar信息 */

#endif

