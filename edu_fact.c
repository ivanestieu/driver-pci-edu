#include "edu_fact.h"

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h> /* module_{init,exit}() */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/pci.h> /* pci_*() */
#include <linux/pci_ids.h> /* pci idents */
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EDU device factorial driver (final project)");
MODULE_AUTHOR("Ivan ESTIEU <ivan.estieu@gmail.com>");

static struct pci_device_id edu_id_table[] = {
    { EDU_VENDOR_ID, EDU_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    {
        0,
    } /* 0 terminated list */
};
MODULE_DEVICE_TABLE(pci, edu_id_table);

static struct class *edu_class;
static int edu_major;

static struct edu_device *edu_devices[NB_MAX_DEVICES];
static int edu_nb;

static const struct file_operations edu_fops = {
    .owner = THIS_MODULE,
    .read = edu_read,
    .write = edu_write,
    .open = edu_open,
    .release = edu_release,
};

static int edu_open(struct inode *inode, struct file *filp)
{
    struct edu_device *dev =
        container_of(inode->i_cdev, struct edu_device, cdev);

    filp->private_data = dev;
    dev_info(&dev->pci_dev->dev, "edu-fact: device opened\n");
    return 0;
}

static int edu_release(struct inode *inode, struct file *filp)
{
    struct edu_device *dev = filp->private_data;

    dev_info(&dev->pci_dev->dev, "edu-fact: device released\n");
    return 0;
}

/*
 * write: receive a decimal number from userspace, trigger factorial
 *        computation and loop until completion.
 */
static ssize_t edu_write(struct file *filp, const char __user *buf,
                         size_t count, loff_t *f_pos)
{
    struct edu_device *dev = filp->private_data;
    char kbuf[32];
    uint32_t val;
    int ret;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;
    kbuf[count] = '\0';

    ret = kstrtou32(kbuf, 10, &val);
    if (ret)
        return ret;

    mutex_lock(&dev->lock);

    dev->result_valid = 0;

    /* Write value to factorial register and start computation */
    iowrite32(val, dev->base + EDU_REG_FACTORIAL);
    iowrite32(0, dev->base + EDU_REG_STATUS);

    /* Poll status register until the computing bit is cleared */
    while (ioread32(dev->base + EDU_REG_STATUS) & EDU_STATUS_COMPUTING)
        msleep(1);

    /* Read back the result */
    dev->result = ioread32(dev->base + EDU_REG_FACTORIAL);
    dev->result_valid = 1;

    mutex_unlock(&dev->lock);

    *f_pos = 0;
    return count;
}

/*
 * read: return the last factorial result as a decimal string.
 */
static ssize_t edu_read(struct file *filp, char __user *buf, size_t count,
                        loff_t *f_pos)
{
    struct edu_device *dev = filp->private_data;
    char kbuf[32];
    int len;

    mutex_lock(&dev->lock);

    if (!dev->result_valid)
    {
        mutex_unlock(&dev->lock);
        return 0;
    }

    len = snprintf(kbuf, sizeof(kbuf), "%u\n", dev->result);

    mutex_unlock(&dev->lock);

    if (*f_pos >= len)
        return 0;

    if (count > len - *f_pos)
        count = len - *f_pos;

    if (copy_to_user(buf, kbuf + *f_pos, count))
        return -EFAULT;

    *f_pos += count;
    return count;
}

static int edu_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{
    struct edu_device *edu_dev;
    int err = 0;
    dev_t dev_num;
    int minor;
    uint32_t ident;

    dev_info(&(dev->dev), "edu-fact: found %04x:%04x\n", ent->vendor,
             ent->device);

    if (edu_nb >= NB_MAX_DEVICES)
    {
        dev_warn(&(dev->dev), "edu-fact: max devices reached\n");
        return -ENODEV;
    }

    /* Alloc private data */
    edu_dev = kzalloc(sizeof(struct edu_device), GFP_KERNEL);
    if (!edu_dev)
    {
        dev_warn(&(dev->dev), "edu-fact: unable to alloc memory\n");
        return -ENOMEM;
    }

    /* set private data */
    edu_dev->pci_dev = dev;
    mutex_init(&edu_dev->lock);
    pci_set_drvdata(dev, edu_dev);

    /* Enable device */
    err = pci_enable_device(dev);
    if (err)
    {
        dev_warn(&(dev->dev), "edu-fact: unable to enable device\n");
        goto err_alloc;
    }

    /* request regions */
    err = pci_request_regions(dev, DEVICE_NAME);
    if (err)
    {
        dev_warn(&(dev->dev), "edu-fact: unable to request regions\n");
        goto err_enable;
    }

    /* map BAR 0 */
    edu_dev->base = pci_iomap(dev, 0, 0);
    if (!edu_dev->base)
    {
        dev_warn(&(dev->dev), "edu-fact:  unable to map BAR0\n");
        err = -EIO;
        goto err_regions;
    }

    /* Sanity-check identification register */
    ident = ioread32(edu_dev->base + EDU_REG_ID);
    dev_info(&(dev->dev), "edu-fact: identification register = 0x%08x\n",
             ident);

    minor = edu_nb;

    /* init cdev with file operations */
    cdev_init(&edu_dev->cdev, &edu_fops);
    edu_dev->cdev.owner = THIS_MODULE;

    /* Add a live char device */
    dev_num = MKDEV(edu_major, minor);
    err = cdev_add(&edu_dev->cdev, dev_num, 1);
    if (err)
    {
        pr_warn("Error %d while trying to add %s%d", err, DEVICE_NAME, minor);
        goto err_cdev;
    }

    /* Create device node */
    struct device *device = device_create(edu_class, &(dev->dev), dev_num, NULL,
                                          DEVICE_NAME "%d", minor);
    if (IS_ERR(device))
    {
        err = PTR_ERR(device);
        pr_warn("Error %d while trying to create %s%d", err, DEVICE_NAME,
                minor);
        goto err_cdev;
    }

    edu_devices[minor] = edu_dev;
    edu_nb++;

    dev_info(&(dev->dev), "edu-fact: ready as /dev/%s%d\n", DEVICE_NAME, minor);
    return 0;

err_cdev:
    device_destroy(edu_class, MKDEV(edu_major, minor));
    cdev_del(&edu_dev->cdev);
err_regions:
    pci_release_regions(dev);
err_enable:
    pci_disable_device(dev);
err_alloc:
    kfree(edu_dev);
    return err;
}

static void edu_remove(struct pci_dev *dev)
{
    struct edu_device *edu_dev = pci_get_drvdata(dev);
    int minor;

    dev_info(&(dev->dev), "edu-fact: device removed\n");

    for (minor = 0; minor < edu_nb; minor++)
    {
        if (edu_devices[minor] == edu_dev)
            break;
    }

    /* release cdev and device node */
    device_destroy(edu_class, MKDEV(edu_major, minor));
    cdev_del(&edu_dev->cdev);

    /* release all resources */
    pci_iounmap(dev, edu_dev->base);
    pci_release_regions(dev);
    pci_disable_device(dev);
    kfree(edu_dev);
}

static struct pci_driver edu_pci_driver = {
    .name = DEVICE_NAME,
    .id_table = edu_id_table,
    .probe = edu_probe, /* Init one device */
    .remove = edu_remove, /* Remove one device */
};

/*
 * Init and Exit
 */
static int __init edu_init(void)
{
    dev_t dev_num;
    int err;

    pr_info("edu-fact: loading module (max %d devices)\n", NB_MAX_DEVICES);

    err = alloc_chrdev_region(&dev_num, 0, NB_MAX_DEVICES, DEVICE_NAME);
    if (err < 0)
    {
        pr_warn("edu-fact: unable to alloc chrdev region\n");
        return err;
    }
    edu_major = MAJOR(dev_num);

    edu_class = class_create(CLASS_NAME);
    if (IS_ERR(edu_class))
    {
        pr_warn("edu-fact: unable to create class\n");
        err = PTR_ERR(edu_class);
        goto err_chrdev;
    }

    /* Register PCI driver */
    err = pci_register_driver(&edu_pci_driver);
    if (err < 0)
    {
        pr_warn("edu-fact: unable to register PCI driver\n");
        goto err_class;
    }

    return 0;

err_class:
    class_destroy(edu_class);
err_chrdev:
    unregister_chrdev_region(MKDEV(edu_major, 0), NB_MAX_DEVICES);
    return err;
}

static void __exit edu_exit(void)
{
    pr_info("Exit edu-fact module\n");

    pci_unregister_driver(&edu_pci_driver);
    class_destroy(edu_class);
    unregister_chrdev_region(MKDEV(edu_major, 0), NB_MAX_DEVICES);
}

module_init(edu_init);
module_exit(edu_exit);
