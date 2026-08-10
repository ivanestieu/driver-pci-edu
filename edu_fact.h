#ifndef EDU_FACT_H
#define EDU_FACT_H

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/pci.h>

/*
 * Supported devices VENDOR_ID/DEVICE_ID
 */
#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x11e8

/* registers */
#define EDU_REG_ID 0x00
#define EDU_REG_FACTORIAL 0x08
#define EDU_REG_STATUS 0x20

#define EDU_STATUS_COMPUTING 0x01 /* computation in progress */

#define DEVICE_NAME "edu-fact"
#define CLASS_NAME "edu"

#define NB_MAX_DEVICES 32

struct edu_device
{
    struct pci_dev *pci_dev;
    void __iomem *base;

    struct cdev cdev;
    struct mutex lock;

    uint32_t result;
    int result_valid;
};

static int edu_open(struct inode *inode, struct file *filp);
static int edu_release(struct inode *inode, struct file *filp);
static ssize_t edu_write(struct file *filp, const char __user *buf,
                         size_t count, loff_t *f_pos);
static ssize_t edu_read(struct file *filp, char __user *buf, size_t count,
                        loff_t *f_pos);

static int edu_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void edu_remove(struct pci_dev *dev);

static int __init edu_init(void);
static void __exit edu_exit(void);

#endif /* EDU_FACT_H */
