#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/gpio.h>

#include "seven_segment.h"

#define DEVICE_NAME     "seven_segment" /* name that will be assigned to this device in /dev fs */
#define USE_DIPLAY_NUM      1   /* minor value of display */
#define USE_NUMBER_OF_DIGITS      1   /* number of digits we are displaying */


struct seven_segment_dev
{
    /* declare struct cdev that will represent
     * our char device in inode structure.
     * inode is used by kernel to represent file objects */
    struct cdev cdev;
    char digit_to_display;
};

/* to implement a char device driver we need to satisfy some
* requirements. one of them is an implementation of mandatory
* methods defined in struct file_operations
*  - read
*  - write
*  - open
*  - release
        * think about device as about a simple file. these are basic
        * operations you do with all regular files. same for char device
* signatures of functions defined in linux/include/fs.h may be
        * called a virtual methods in OOP terminology, that you need to
* implemt. all these are repsented as callback functions
*/
static int seven_segment_open(struct inode *inode, struct file *filp);
static int seven_segment_release(struct inode *inode, struct file *filp);
static ssize_t seven_segment_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t seven_segment_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

/* declare structure seven_segment_fops which holds
 * our implementations of callback functions,
 * in instance of struct file_operations for our
 * char device driver
 */
static struct file_operations seven_segment_fops = {
        .owner = THIS_MODULE,
        .open = seven_segment_open,
        .release = seven_segment_release,
        .read = seven_segment_read,
        .write = seven_segment_write,
};

/* declare prototypes of init and exit functions.
 * implementation of these 2 functions is mandatory
 * for each linux kernel module. they serve to
 * satisfy kernel request to initialize module
 * when you insmod'ing it in system and release
 * allocated resources when you prform rmmod command */
static int seven_segment_init(void);
static void seven_segment_exit(void);


/* declare pointer to seven_segment_dev device structure objects
 * which represent each of our pins as a char device */
struct seven_segment_dev *seven_segment_devp;

static dev_t first;

static char *message = NULL;


static ssize_t digit_to_display_show(struct class *cls, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%c\n", '0' + seven_segment_devp->digit_to_display);
};

static ssize_t digit_to_display_store(struct class *cls, struct class_attribute *attr,
        const char *buf, size_t count)
{
    if (buf[0] - (int)'0' < 10 && buf[0]-(int)'0' >= 0) {
        seven_segment_devp->digit_to_display = buf[0] - (int)'0';
        display_on_screen(buf);
    }
    return 1;
};

static CLASS_ATTR_RW(digit_to_display);
static struct attribute *class_attr_attrs[] = { &class_attr_digit_to_display.attr, NULL };
ATTRIBUTE_GROUPS(class_attr);

static struct class seven_segment_class = {
    .name = DEVICE_NAME,
    .owner = THIS_MODULE,
    .class_groups = class_attr_groups,
};


/*
* seven_segment_open - Open device
* this is implementation of previously declared function
* open from our file_operations structure.
* this function will be called each time device recives
* open file operation applied to its name in /dev
* open method call creates struct file instance
*/

static int seven_segment_open(struct inode *inode, struct file *filp)
{
    struct seven_segment_dev *seven_segment_devp;

    seven_segment_devp = container_of(inode->i_cdev, struct seven_segment_dev, cdev);

    /* assign a pointer to struct representing our
     * device to its corresponding file object */
    filp->private_data = seven_segment_devp;

    /* zero returns stand for success in kernel programming */
    return 0;
}

static int seven_segment_release(struct inode *inode, struct file *filp)
{

    filp->private_data = NULL;

    return 0;
}

static ssize_t seven_segment_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval;
    char byte;
    struct seven_segment_dev *seven_segment_devp = filp->private_data;



    for (retval = 0; retval < count; ++retval) {
        byte = '0' + seven_segment_devp->digit_to_display;
        /* use special macro to copy data from kernel space
        * co user space. API related to user space
        * interactions are found in arm/asm/uaccess.h
        */
        if (put_user(byte, buf + retval))
            break;
    }
    return retval;
}

static ssize_t seven_segment_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    struct seven_segment_dev *seven_segment_devp = filp->private_data;
    memset(message, 0, USE_NUMBER_OF_DIGITS);
    if (copy_from_user(message, buf, count) != 0)   // raw copy
        return -EFAULT;

    seven_segment_devp->digit_to_display = message[0] - (int)'0';

    display_on_screen(message);

    f_pos += count;
    return count;
}


static int __init seven_segment_init(void)
{
    int ret = 0;

    if (alloc_chrdev_region(&first, 0, USE_DIPLAY_NUM, DEVICE_NAME) < 0)
    {
        printk(KERN_DEBUG "Cannot register device\n");
        return -1;
    }

    if (class_register(&seven_segment_class) < 0)
    {
        printk(KERN_DEBUG "Cannot create class %s\n", DEVICE_NAME);
        unregister_chrdev_region(first, USE_DIPLAY_NUM);
        return -EINVAL;
    }
    /* allocate memory for sctuctures to contain display info representation
               */
    seven_segment_devp = kmalloc(sizeof(struct seven_segment_dev), GFP_KERNEL);
    if (!seven_segment_devp)
    {
        printk(KERN_DEBUG "[seven_segment]Bad kmalloc\n");
        return -ENOMEM;
    }

    seven_segment_devp->digit_to_display = '1'- (int)'0';
    seven_segment_devp->cdev.owner = THIS_MODULE;

    /* itialize cdev structure for our device and match it
             * with file_operations defined for it
    */
    cdev_init(&seven_segment_devp->cdev, &seven_segment_fops);

    if ((ret = cdev_add( &seven_segment_devp->cdev, first, 1)))
    {
        printk (KERN_ALERT "[seven_segment] - Error %d adding cdev\n", ret);
        /* clean up in opposite way from init
                */
        device_destroy (&seven_segment_class, first);
        class_destroy(&seven_segment_class);
        unregister_chrdev_region(first, USE_DIPLAY_NUM);
        return ret;
    }

    if (device_create( &seven_segment_class,
                       NULL,
                       first,
                       NULL,
                       "seven_segment") == NULL)
    {
        class_destroy(&seven_segment_class);
        unregister_chrdev_region(first, USE_DIPLAY_NUM);

        return -1;
    }
    //malloc memory for message for display
    message = (char *)kmalloc(USE_NUMBER_OF_DIGITS, GFP_KERNEL);
    printk("[seven_segment] - Driver initialized\n");
    return 0;
}


static void __exit seven_segment_exit(void)
{
    unregister_chrdev_region(first, USE_DIPLAY_NUM);
    /* free up memory used by device structures
         */
    kfree(seven_segment_devp);

    device_destroy ( &seven_segment_class, MKDEV(MAJOR(first), MINOR(first)));
    /* destroy class
    */
    class_destroy(&seven_segment_class);
    printk(KERN_INFO "[seven_segment] - Raspberry Pi 7-segment driver removed\n");
}

module_init(seven_segment_init);
module_exit(seven_segment_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Usachova Victoria");
MODULE_DESCRIPTION("Loadable Kernel Module - Linux device driver for Raspberry Pi");