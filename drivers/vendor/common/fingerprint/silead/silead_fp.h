/*
 * @file   silead_fp.h
 * @brief  Contains silead_fp device head file.
 *
 *
 * Copyright 2016-2021 GigaDevice/Silead Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * ------------------- Revision History ------------------------------
 * <author>    <date>   <version>     <desc>
 * Bill Yu    2018/5/2    0.1.0      Init version
 * Bill Yu    2018/5/28   0.1.1      Disable netlink if netlink id = 0
 * Bill Yu    2018/6/1    0.1.2      Support wakelock
 * Bill Yu    2018/6/5    0.1.3      Support chip enter power down
 * Bill Yu    2018/6/7    0.1.4      Support create proc node
 * Bill Yu    2018/6/27   0.1.5      Expand pwdn I/F
 * Rui Wu     2019/10/10  0.1.6      Support create class node
 * Bill Yu    2020/5/11   0.1.7      Netlink id should < 32
 * Bill Yu    2020/5/29   0.1.8      Default use poll
 * Bill Yu    2020/12/22  0.1.9      Support 5.x version wakeup
 * Melvin cao 2021/01/29  0.2.0      Support silead spi device driver
 */

#ifndef __SILEAD_FP_H__
#define __SILEAD_FP_H__

#include <linux/printk.h>
#include <linux/version.h>

#include <linux/list.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <net/sock.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/cdev.h>

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#else
#include <linux/notifier.h>
#endif

#ifdef CONFIG_VENDOR_SOC_SPRD_COMPILE
#define  USE_PLATFORM_BUS	1
#include <linux/platform_device.h>
#endif

#ifdef CONFIG_VENDOR_SOC_MTK_COMPILE
#define  USE_SPI_BUS   1
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
/*define SUPPORT_REE_SPI  */
#endif

#define FEATURE_FLASH_CS      0x01
/*#define PROC_DIR		"fp" */      /*  if defined, create node under /proc/fp/xxx */
#define PROC_NODE		"fp_id"   /* proc node name */
#define RESET_TIME            2	/* Default chip reset wait time(ms) */
#define RESET_TIME_MULTIPLE   1 /* Multiple for reset time multiple*wait_time */
#define SIFP_NETLINK_ROUTE    0 /* 0: poll/1-31: netlink(cat /proc/net/netlink)/ > 31: epoll/ 30 */
#define NL_MSG_LEN            16
#define PKG_SIZE 4
/*#define OLD_NAV */             /*input_sync method*/
#define NEW_NAV                  /*kobject method*/
/*#define CLASS_NODE   "fingerprint" */ /* if defined, create class node /sys/class/fingerprint/fingerprint */

#if (SIFP_NETLINK_ROUTE > 0) && (SIFP_NETLINK_ROUTE < 32)
#define BSP_SIL_NETLINK
#endif

#ifndef BSP_SIL_NETLINK
struct silfp_msg_list {
	unsigned char msg;
	struct list_head list;
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#define VERIFY_READ   0
#define VERIFY_WRITE  1
#define ACCESS_OK(t,x,y)  access_ok(x, y)
#else
#define ACCESS_OK(t,x,y)  access_ok(t, x, y)
#endif

#ifndef _LINUX_WAKELOCK_H

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#define USE_WAKEUP_REG
#endif

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_TYPE_COUNT
};

#ifdef USE_WAKEUP_REG
struct wake_lock {
    struct wakeup_source  *ws;
};

#define ws_init(s, n)     s = wakeup_source_register(NULL, n)
#define ws_deinit(s)      wakeup_source_unregister(s)
#define ws_lock(s)        __pm_stay_awake(s)
#define ws_lock_tm(s, t)  __pm_wakeup_event(s, t)
#define ws_unlock(s)      __pm_relax(s)

#else
struct wake_lock {
    struct wakeup_source  ws;
};

#define ws_init(s, n)     wakeup_source_init(&s, n)
#define ws_deinit(s)      wakeup_source_trash(&s)
#define ws_lock(s)        __pm_stay_awake(&s)
#define ws_lock_tm(s, t)  __pm_wakeup_event(&s, t)
#define ws_unlock(s)      __pm_relax(&s)

#endif /* USE_WAKEUP_REG */

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
    ws_init(lock->ws, name);
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
    ws_deinit(lock->ws);
}

static inline void wake_lock(struct wake_lock *lock)
{
    ws_lock(lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
    ws_lock_tm(lock->ws, jiffies_to_msecs(timeout));
}

static inline void wake_unlock(struct wake_lock *lock)
{
    ws_unlock(lock->ws);
}
#endif /* _LINUX_WAKELOCK_H */

enum _pwdn_mode_t {
	SIFP_PWDN_NONE = 0,
	SIFP_PWDN_POWEROFF = 1, /* shutdown the avdd power supply */
	SIFP_PWDN_FLASH = 2, /* shutdown avdd 200ms for H/W full reset */
	SIFP_PWDN_MAX,
};

enum _fp_nav_key_v_t {
	NAV_KEY_UNKNOWN = 0,
	NAV_KEY_START = 1,
	NAV_KEY_UP = NAV_KEY_START,
	NAV_KEY_DOWN,
	NAV_KEY_RIGHT,
	NAV_KEY_LEFT,
	NAV_KEY_CLICK,
	NAV_KEY_DCLICK,
	NAV_KEY_LONGPRESS,
	NAV_KEY_CLICK_DOWN,
	NAV_KEY_CLICK_UP,
	NAV_KEY_MAX,
	NAV_KEY_WAITMORE = 1000,
};

#define FP_NAV_UP	   "fp_nav_event_up=true"
#define FP_NAV_DOWN	   "fp_nav_event_down=true"
#define FP_NAV_LEFT	   "fp_nav_event_left=true"
#define FP_NAV_RIGHT	   "fp_nav_event_right=true"

#define IS_KEY_VALID(k) ((k) > NAV_KEY_UNKNOWN && (k) < NAV_KEY_MAX)

struct fp_dev_kmap_t {
	uint16_t k[NAV_KEY_MAX - NAV_KEY_START]; /* Up/Down/Right/Left/Click/Double Click/Longpress */
};

struct silfp_data {
	dev_t devt;
	struct cdev cdev;
	spinlock_t  spi_lock;

#if defined(USE_SPI_BUS)
	struct spi_device *spi;
#elif defined(USE_PLATFORM_BUS)
	struct platform_device	*spi;
#endif
	/*zte_fp_nav device*/
	struct platform_device *nav_dev;

	struct list_head  device_entry;

	unsigned	users;

	struct device *dev;
	int ref;

	struct input_dev *input;

	spinlock_t irq_lock;
	int int_port;
	int irq;
	s32 irq_is_disable;
	int   irq_ignore;
	s32 power_is_off;
	int rst_port;
	struct work_struct  work;
	struct completion done;
	struct wake_lock wakelock;
	struct wake_lock wakelock_hal;

#ifdef BSP_SIL_NETLINK
	/* for netlink use */
	struct sock *nl_sk;
#else
	spinlock_t  read_lock;
	wait_queue_head_t read_queue;
	struct list_head msg_q;
#endif

	struct fp_dev_kmap_t keymap_cust;
	int scr_off;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend es;
#else
	struct notifier_block notif;
#endif /* CONFIG_HAS_EARLYSUSPEND */

	/* for power supply */
	int avdd_port;

	/* 0 GPIO , 1 LDO */
	int power_type;
	struct regulator *fp_reg;
	/* get the voltage from dts file */
	int power_voltage;

#ifdef PROC_NODE
	struct proc_dir_entry *proc_root;
	struct proc_dir_entry *proc_entry;
#endif /* PROC_NODE */

	/* for spi enable/disable */
	atomic_t  spionoff_count;

	atomic_t  init;
};

/* -------------------------------------------------------------------- */
/*                            debug settings                            */
/* -------------------------------------------------------------------- */
typedef enum {
	ERR_LOG = 0,
	WARN_LOG,
	INFO_LOG,
	DEBUG_LOG,
	ALL_LOG,
} fp_debug_level_t;

static fp_debug_level_t debug_level = INFO_LOG;

#define LOG_MSG_DEBUG(level, fmt, args...) do { \
			if (debug_level >= level) {\
				pr_warn("[sileadfp_info] " fmt, ##args); \
			} \
		} while (0)

enum _netlink_cmd_t {
	SIFP_NETLINK_START = 0,
	SIFP_NETLINK_IRQ = 1,
	SIFP_NETLINK_SCR_OFF,
	SIFP_NETLINK_SCR_ON,
	SIFP_NETLINK_CONNECT,
	SIFP_NETLINK_DISCONNECT,
	SIFP_NETLINK_TP_TOUCHDOWN,
	SIFP_NETLINK_TP_TOUCHUP,
	SIFP_NETLINK_MAX,
};

enum _fp_nav_key_f_t {
	NAV_KEY_FLAG_UP = 0,
	NAV_KEY_FLAG_DOWN,
	NAV_KEY_FLAG_CLICK,
};

struct fp_dev_key_t {
	int value;
	uint32_t flag;   /* key down = 1, key up = 0, key down+up = 2 */
};

#define DEVNAME_LEN  16

struct fp_dev_init_t {
	uint8_t mode;
	uint8_t bits;
	uint16_t delay;
	uint32_t speed;
	char dev[DEVNAME_LEN];
	uint8_t nl_id;
	uint8_t dev_id;
	uint16_t reserve;
	uint32_t reg;
	char ta[DEVNAME_LEN];
};

struct fp_dev_debug_t {
	uint8_t cmd[4];
};

struct fp_dev_touch_info {
	uint8_t touch_state;
	uint8_t area_rate;
	uint16_t x;
	uint16_t y;
};

#define DEVICE "/dev/silead_fp"

#define PROC_VND_ID_LEN   32

#define SIFP_IOC_MAGIC	's'
#define SIFP_IOC_RESET        _IOW(SIFP_IOC_MAGIC, 10, u8)
#define SIFP_IOC_ENABLE_IRQ   _IO(SIFP_IOC_MAGIC,  11)
#define SIFP_IOC_DISABLE_IRQ  _IO(SIFP_IOC_MAGIC,  12)
#define SIFP_IOC_WAIT_IRQ     _IOR(SIFP_IOC_MAGIC, 13, u8)
#define SIFP_IOC_CLR_IRQ      _IO(SIFP_IOC_MAGIC,  14)
/*#define SPFP_IOC_EXIT       _IOR(SIFP_IOC_MAGIC, 1, u8)*/
#define SIFP_IOC_KEY_EVENT    _IOW(SIFP_IOC_MAGIC, 15, struct fp_dev_key_t)
#define SIFP_IOC_INIT         _IOR(SIFP_IOC_MAGIC, 16, struct fp_dev_init_t)
#define SIFP_IOC_DEINIT       _IO(SIFP_IOC_MAGIC,  17)
#define SIFP_IOC_IRQ_STATUS   _IOR(SIFP_IOC_MAGIC, 18, u8)
#define SIFP_IOC_DEBUG        _IOR(SIFP_IOC_MAGIC, 19, struct fp_dev_debug_t)
#define SIFP_IOC_SCR_STATUS   _IOR(SIFP_IOC_MAGIC, 20, u8)
#define SIFP_IOC_GET_VER      _IOR(SIFP_IOC_MAGIC, 21, char[10])
#define SIFP_IOC_SET_KMAP     _IOW(SIFP_IOC_MAGIC, 22, uint16_t[7])
#define SIFP_IOC_ACQ_SPI      _IO(SIFP_IOC_MAGIC,  23)
#define SIFP_IOC_RLS_SPI      _IO(SIFP_IOC_MAGIC,  24)
#define SIFP_IOC_PKG_SIZE     _IOR(SIFP_IOC_MAGIC, 25, u8)
#define SIFP_IOC_DBG_LEVEL    _IOWR(SIFP_IOC_MAGIC, 26, u8)
#define SIFP_IOC_WAKELOCK     _IOW(SIFP_IOC_MAGIC, 27, u8)
#define SIFP_IOC_PWDN         _IOW(SIFP_IOC_MAGIC, 28, u8)
#define SIFP_IOC_PROC_NODE    _IOW(SIFP_IOC_MAGIC, 29, char[PROC_VND_ID_LEN])
#define SIFP_IOC_SET_FEATURE  _IOW(SIFP_IOC_MAGIC, 30, u8)

#ifndef INIT_COMPLETION
#define INIT_COMPLETION(x)  ((x).done = 0)
#endif
#define reinit_completion(x) INIT_COMPLETION(*(x))

int silfp_parse_dts(struct silfp_data *fp_dev);
void silfp_cleanup(struct silfp_data *fp_dev);
void silfp_hw_poweron(struct silfp_data *fp_dev);
void silfp_hw_poweroff(struct silfp_data *fp_dev);
void silfp_hw_reset(struct silfp_data *fp_dev, u8 delay);
void silfp_set_spi(struct silfp_data *fp_dev, bool enable);
int silfp_resource_init(struct silfp_data *fp_dev, struct fp_dev_init_t *dev_info);
void silfp_irq_disable(struct silfp_data *fp_dev);
void silfp_irq_enable(struct silfp_data *fp_dev);
irqreturn_t silfp_irq_handler(int irq, void *dev_id);
int silfp_input_init(struct silfp_data *fp_dev);
void silfp_work_func(struct work_struct *work);
int silfp_wait_irq(struct silfp_data *fp_dev);
int silfp_irq_status(struct silfp_data *fp_dev);
int silfp_resource_deinit(struct silfp_data *fp_dev);
int silfp_set_feature(struct silfp_data *fp_dev, u8 feature);
int silfp_irq_to_reset_init(struct silfp_data *fp_dev);
void silfp_pwdn(struct silfp_data *fp_dev, u8 flag_avdd);

#endif /* __SILEAD_FP_H__ */
/* End of file silead_fp.h */
