#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/i2c.h>
#include <linux/input.h>

#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/export.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/uuid.h>
#include <linux/ctype.h>
#include <net/sock.h>
#include <net/net_namespace.h>


struct sock *sk = NULL;

struct device *dev = NULL;
char * s_c[2];

void bt_monitor_send(char *buf)
{
	s_c[0] = buf;
	s_c[1] = NULL;
	//kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, s_c);

	struct sk_buff *skb;

	if (netlink_has_listeners(sk, 1)) {
	}

	skb = alloc_skb(strlen(buf) + 1, GFP_KERNEL);
	if (skb) {
		char *scratch;
		/* add header */
		scratch = skb_put(skb, strlen(buf) + 1);
		sprintf(scratch, "%s", buf);

		NETLINK_CB(skb).dst_group = 1;

		if (netlink_broadcast(sk, skb, 0, 1, GFP_KERNEL) != 0)
			printk(KERN_ERR "song_event: netlink_broadcast fail\n");
	} 
}
EXPORT_SYMBOL(bt_monitor_send);

static ssize_t send(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	s_c[0] = "bt_monitor_hello";
	s_c[1] = NULL;
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, s_c);
	return count;
}
static DEVICE_ATTR(S, S_IRUGO|S_IWUSR, NULL, send);


static const struct attribute *song_event_attr[] = {
			&dev_attr_S.attr,
			NULL,
};


static const struct attribute_group song_event_attr_group = {
			.attrs = (struct attribute **) song_event_attr,
};


static struct class song_event_class = {
        .name =         "song_event",
        .owner =        THIS_MODULE,
};


static int __init song_uevent_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.groups	= 1,
		.flags	= NL_CFG_F_NONROOT_RECV,
	};

	int ret = 0;
	ret = class_register(&song_event_class);
	if (ret < 0) {
		printk(KERN_ERR "song_event: class_register fail\n");
	return ret;
	}


	dev = device_create(&song_event_class, NULL, MKDEV(0, 0), NULL, "song_event");
	if (dev) {
		ret = sysfs_create_group(&dev->kobj, &song_event_attr_group);
		if(ret < 0) {
			printk(KERN_ERR "song_event:sysfs_create_group fail\r\n");
			return ret;
		}
	} else {
		printk(KERN_ERR "song_event:device_create fail\r\n");
		ret = -1;
		return ret;
	}


	sk = netlink_kernel_create(&init_net, 30, &cfg);
	if (!sk) {
		printk(KERN_ERR
		       "song_event: unable to create netlink socket!\n");
		kfree(sk);
		return -ENODEV;
	}


	return 0;
}
module_init(song_uevent_init);

MODULE_AUTHOR("zte <zte@zte.com.cn>");
MODULE_DESCRIPTION("bt monitor driver");
MODULE_LICENSE("GPL");
