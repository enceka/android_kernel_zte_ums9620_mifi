#ifndef CTS_PLATFORM_H
#define CTS_PLATFORM_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/unaligned/access_ok.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/spinlock.h>
#include <linux/rtmutex.h>
#include <linux/byteorder/generic.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/suspend.h>
#if (KERNEL_VERSION(4, 14, 0) < LINUX_VERSION_CODE)
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/firmware.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/wait.h>
#include <linux/syscalls.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#endif /* CONFIG_OF */

#include <linux/fb.h>
#include <linux/notifier.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#ifdef CFG_CTS_DRM_NOTIFIER
#include <linux/msm_drm_notify.h>
#endif

#include "cts_config.h"
#include "cts_core.h"

extern bool cts_show_debug_log;

#ifndef LOG_TAG
#define LOG_TAG         ""
#endif /* LOG_TAG */

enum cts_driver_log_level {
	CTS_DRIVER_LOG_ERROR,
	CTS_DRIVER_LOG_WARN,
	CTS_DRIVER_LOG_INFO,
	CTS_DRIVER_LOG_DEBUG,
};

extern int cts_start_driver_log_redirect(const char *filepath, bool append_to_file,
		char *log_buffer, int log_buf_size, int log_level);
extern void cts_stop_driver_log_redirect(void);
extern int cts_get_driver_log_redirect_size(void);
/* extern void cts_log(int level, const char *fmt, ...); */

#define cts_err(fmt, args...)   \
	pr_err("CTS-ERR :%s:"fmt"\n",  __func__, ##args)
#define cts_warn(fmt, args...)  \
	pr_warn("CTS-WARN:%s:"fmt"\n",  __func__, ##args)
#define cts_info(fmt, args...)  \
	pr_info("CTS-INFO:%s:"fmt"\n",  __func__, ##args)
#define cts_dbg(fmt, args...)  \
	do {									\
		if (cts_show_debug_log)						\
			pr_info("CTS-DBG:%s:"fmt"\n",  __func__, ##args);\
	} while (0)

struct cts_device;
struct cts_device_touch_msg;
struct cts_device_gesture_info;

struct cts_platform_data {
	int irq;
	int int_gpio;
#ifdef CFG_CTS_HAS_RESET_PIN
	int rst_gpio;
#endif /* CFG_CTS_HAS_RESET_PIN */

	u32 res_x;
	u32 res_y;

#ifdef CONFIG_CTS_VIRTUALKEY
	u8  vkey_num;
	u8  vkey_state;
	u8  vkey_keycodes[CFG_CTS_MAX_VKEY_NUM];
#endif /* CONFIG_CTS_VIRTUALKEY */

	struct cts_device *cts_dev;

	struct input_dev *ts_input_dev;

#ifdef CFG_CTS_HANDLE_IRQ_USE_WORKQUEUE
	struct work_struct ts_irq_work;
#endif /* CFG_CTS_HANDLE_IRQ_USE_WORKQUEUE */
#ifdef CFG_CTS_HANDLE_IRQ_USE_KTHREAD
	struct task_struct *irq_thread;
	int irq_pending;
	wait_queue_head_t irq_waitq_head;
#endif /* CFG_CTS_HANDLE_IRQ_USE_KTHREAD */

	struct mutex dev_lock;
	struct spinlock irq_lock;
	bool            irq_is_disable;

#ifdef CFG_CTS_GESTURE
	u8  gesture_num;
	u8  gesture_keymap[CFG_CTS_NUM_GESTURE][2];
	bool irq_wake_enabled;
#endif /* CFG_CTS_GESTURE */

#ifdef CFG_CTS_CHARGER_DETECT
	struct notifier_block charger_notifier;
#endif /* CFG_CTS_CHARGER_DETECT */

#ifdef CONFIG_CTS_PM_FB_NOTIFIER
	struct notifier_block fb_notifier;
#endif /* CONFIG_CTS_PM_FB_NOTIFIER */

#ifdef CFG_CTS_MAKEUP_EVENT_UP
	struct delayed_work makeup_event_up_work;
#endif /* CFG_CTS_MAKEUP_EVENT_UP */

#ifdef CFG_CTS_FW_LOG_REDIRECT
	u8 fw_log_buf[CTS_FW_LOG_BUF_LEN];
#endif

#ifdef CONFIG_CTS_I2C_HOST
	struct i2c_client *i2c_client;
	u8 i2c_fifo_buf[CFG_CTS_MAX_I2C_XFER_SIZE];
#endif /* CONFIG_CTS_I2C_HOST */
#ifdef CONFIG_CTS_SPI_HOST
	struct spi_device *spi_client;
	u8 spi_cache_buf[ALIGN(CFG_CTS_MAX_SPI_XFER_SIZE+10, 4)];
	u8 spi_rx_buf[ALIGN(CFG_CTS_MAX_SPI_XFER_SIZE+10, 4)];
	u8 spi_tx_buf[ALIGN(CFG_CTS_MAX_SPI_XFER_SIZE+10, 4)];
	u32 spi_speed;
#endif /* CONFIG_CTS_SPI_HOST */
	struct cts_device_touch_info touch_info;
};

#ifdef CONFIG_CTS_I2C_HOST
extern size_t cts_plat_get_max_i2c_xfer_size(struct cts_platform_data *pdata);
extern u8 *cts_plat_get_i2c_xfer_buf(struct cts_platform_data *pdata,
		size_t xfer_size);
extern int cts_plat_i2c_write(struct cts_platform_data *pdata, u8 i2c_addr,
		const void *src, size_t len, int retry, int delay);
extern int cts_plat_i2c_read(struct cts_platform_data *pdata, u8 i2c_addr,
		const u8 *wbuf, size_t wlen, void *rbuf, size_t rlen,
		int retry, int delay);
extern int cts_plat_is_i2c_online(struct cts_platform_data *pdata, u8 i2c_addr);
#endif /* CONFIG_CTS_I2C_HOST */
#ifdef CONFIG_CTS_SPI_HOST
extern size_t cts_plat_get_max_spi_xfer_size(struct cts_platform_data *pdata);
extern u8 *cts_plat_get_spi_xfer_buf(struct cts_platform_data *pdata, size_t xfer_size);
extern int cts_plat_spi_write(struct cts_platform_data *pdata, u8 i2c_addr, const void *src,
		size_t len, int retry, int delay);
extern int cts_plat_spi_read(struct cts_platform_data *pdata, u8 i2c_addr,
		const u8 *wbuf, size_t wlen, void *rbuf, size_t rlen,
		int retry, int delay);
extern int cts_plat_spi_read_delay_idle(struct cts_platform_data *pdata, u8 dev_addr,
		const u8 *wbuf, size_t wlen, void *rbuf, size_t rlen,
		int retry, int delay, int idle);
extern int cts_spi_send_recv(struct cts_platform_data *pdata, size_t len, u8 *tx_buffer, u8 *rx_buffer);
extern int cts_plat_is_normal_mode(struct cts_platform_data *pdata);
#endif /* CONFIG_CTS_SPI_HOST */

int cts_init_platform_data(struct cts_platform_data *pdata,
		struct device *device, enum cts_bus_type bus_type);
extern int cts_deinit_platform_data(struct cts_platform_data *pdata);
extern int cts_plat_request_resource(struct cts_platform_data *pdata);
extern void cts_plat_free_resource(struct cts_platform_data *pdata);

extern int cts_plat_request_irq(struct cts_platform_data *pdata);
extern void cts_plat_free_irq(struct cts_platform_data *pdata);
extern int cts_plat_enable_irq(struct cts_platform_data *pdata);
extern int cts_plat_disable_irq(struct cts_platform_data *pdata);

#ifdef CFG_CTS_HAS_RESET_PIN
extern int cts_plat_reset_device(struct cts_platform_data *pdata);
#else /* CFG_CTS_HAS_RESET_PIN */
static inline int cts_plat_reset_device(struct cts_platform_data *pdata) { return 0; }
#endif /* CFG_CTS_HAS_RESET_PIN */

extern int cts_plat_init_touch_device(struct cts_platform_data *pdata);
extern void cts_plat_deinit_touch_device(struct cts_platform_data *pdata);
extern int cts_plat_process_touch_msg(struct cts_platform_data *pdata,
		struct cts_device_touch_msg *msgs, int num);
extern int cts_plat_release_all_touch(struct cts_platform_data *pdata);

#ifdef CONFIG_CTS_VIRTUALKEY
extern int cts_plat_init_vkey_device(struct cts_platform_data *pdata);
extern void cts_plat_deinit_vkey_device(struct cts_platform_data *pdata);
extern int cts_plat_process_vkey(struct cts_platform_data *pdata, u8 vkey_state);
extern int cts_plat_release_all_vkey(struct cts_platform_data *pdata);
#else /* CONFIG_CTS_VIRTUALKEY */
static inline int cts_plat_init_vkey_device(struct cts_platform_data *pdata) { return 0; }
static inline void cts_plat_deinit_vkey_device(struct cts_platform_data *pdata) {}
static inline int cts_plat_process_vkey(struct cts_platform_data *pdata, u8 vkey_state) { return 0; }
static inline int cts_plat_release_all_vkey(struct cts_platform_data *pdata) { return 0; }
#endif /* CONFIG_CTS_VIRTUALKEY */

#ifdef CFG_CTS_GESTURE
extern int cts_plat_enable_irq_wake(struct cts_platform_data *pdata);
extern int cts_plat_disable_irq_wake(struct cts_platform_data *pdata);

extern int cts_plat_init_gesture(struct cts_platform_data *pdata);
extern void cts_plat_deinit_gesture(struct cts_platform_data *pdata);
extern int cts_plat_process_gesture_info(struct cts_platform_data *pdata,
		struct cts_device_gesture_info *gesture_info);
#else /* CFG_CTS_GESTURE */
static inline int cts_plat_init_gesture(struct cts_platform_data *pdata) { return 0; }
static inline void cts_plat_deinit_gesture(struct cts_platform_data *pdata) {}
#endif /* CFG_CTS_GESTURE */

#ifdef CFG_CTS_FW_LOG_REDIRECT
extern size_t cts_plat_get_max_fw_log_size(struct cts_platform_data *pdata);
extern u8 *cts_plat_get_fw_log_buf(struct cts_platform_data *pdata, size_t size);
#endif

extern int cts_plat_set_reset(struct cts_platform_data *pdata, int val);
extern int cts_plat_get_int_pin(struct cts_platform_data *pdata);

#endif /* CTS_PLATFORM_H */

