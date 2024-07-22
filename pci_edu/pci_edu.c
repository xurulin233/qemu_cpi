#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>

#define MY_PCI_DEVICE_ID	    0x11e8
#define MY_PCI_VENDOR_ID	    0x1234
#define MY_PCI_REVISION_ID	0x10



static struct pci_device_id ids[] = {
	{ PCI_DEVICE(MY_PCI_VENDOR_ID, MY_PCI_DEVICE_ID), },
	{ 0 , }
};

static struct my_pci_info_t {
	struct pci_dev *dev;
	void __iomem *address_bar0;
} pci_info;

MODULE_DEVICE_TABLE(pci, ids);



static irqreturn_t pci_irq_handler(int irq, void *dev_info)
{
	struct my_pci_info_t *_pci_info = dev_info;

	*((uint32_t *)(_pci_info->address_bar0 + 0x64)) = 0x01;
	printk("my pci:receive irq\n");

	return 0;
}


static int pcie_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int bar = 0;
	int ret;
	resource_size_t len;
	
	ret = pci_enable_device(dev);
	if(ret) {
		return ret;
	}
	
	len = pci_resource_len(dev, bar);
	pci_info.address_bar0 = pci_iomap(dev, bar, len);
	pci_info.dev = dev;
	
	// register interrupt
	ret = request_irq(dev->irq, pci_irq_handler, IRQF_SHARED, "my_pci", &pci_info);
	if(ret) {
		printk("request IRQ failed.\n");
		return 1;
	}

	// enable irq for finishing factorial computation
	*((uint32_t *)(pci_info.address_bar0 + 0x10)) = 0x01;

	return 0;
}



static void pcie_remove(struct pci_dev *dev)
{
	// disable irq for finishing factorial computation
	*((uint32_t *)(pci_info.address_bar0 + 0x20)) = 0x01;
	
	free_irq(dev->irq, &pci_info);
	
	pci_iounmap(dev, pci_info.address_bar0);
	
	pci_disable_device(dev);
}



static struct pci_driver pci_driver = {
	.name		= "pci_edu",
	.id_table	= ids,
	.probe		= pcie_probe,
	.remove		= pcie_remove,
};


static int __init pci_init(void)
{
	return pci_register_driver(&pci_driver);
}



static void __exit pci_exit(void)
{
	pci_unregister_driver(&pci_driver);
}

MODULE_LICENSE("GPL");
module_init(pci_init);
module_exit(pci_exit);


