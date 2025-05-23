/*
 * GalaxyCore touchscreen driver
 *
 * Copyright (C) 2021 GalaxyCore Incorporated
 *
 * Copyright (C) 2021 Neo Chen <neo_chen@gcoreinc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef GCORE_TPD_COMMON_H_
#define GCORE_TPD_COMMON_H_

/*----------------------------------------------------------*/
/* INCLUDE FILE                                             */
/*----------------------------------------------------------*/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>

#include <linux/cdev.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include "gcore_firmware_config.h"
#include "ztp_common.h"
#include <uapi/linux/sched/types.h>

/*----------------------------------------------------------*/
/* GALAXYCORE TOUCH DEVICE DRIVER RELEASE VERSION           */
/*----------------------------------------------------------*/
#define TOUCH_DRIVER_RELEASE_VERISON   ("1.0.0.0")

/*
 * Note.
 * Misc Debug Option
 */
/* #define CONFIG_DEBUG_SPI_SPEED */

#ifdef CONFIG_TOUCH_DRIVER_RUN_ON_MTK_PLATFORM
#include "tpd.h"
#ifdef CONFIG_MTK_LEGACY_PLATFORM
#include "mtk_spi.h"
#endif
#endif

/*
 * Log define
 */
#define GTP_ERROR(fmt, arg...)          pr_err("<GTP-ERR>[%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#define GTP_INFO(fmt, arg...)          pr_info("<GTP-INFO>[%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#define GTP_DEBUG(fmt, arg...)				\
	do {									\
		if (CONFIG_ENABLE_DEBUG_LOG)						\
			pr_err("<GTP-DBG>[%s:%d]"fmt"\n", __func__, __LINE__, ##arg);\
	} while (0)

#define GTP_DRIVER_NAME               "gcore"
#define GTP_MAX_TOUCH                 10
#define DEMO_DATA_SIZE                (6 * GTP_MAX_TOUCH + 1 + 2 + 2)

static const struct of_device_id tpd_of_match[] = {
	{.compatible = "gcore,touchscreen"},
	{},
};

#define I2C_BUF_SIZE 2048

/*
 * Raw Data
 */

#if defined(CONFIG_ENABLE_CHIP_TYPE_GC7371)
#define RAW_DATA_SIZE               (1296)
#define RAWDATA_ROW					(36)
#define RAWDATA_COLUMN				(18)
#elif defined(CONFIG_ENABLE_CHIP_TYPE_GC7271)
#define RAW_DATA_SIZE               (1080)
#define RAWDATA_ROW					(36)
#define RAWDATA_COLUMN				(15)
#elif defined(CONFIG_ENABLE_CHIP_TYPE_GC7202)
#define RAW_DATA_SIZE               (1152)
#define RAWDATA_ROW					(32)
#define RAWDATA_COLUMN				(18)
#elif defined(CONFIG_ENABLE_CHIP_TYPE_GC7372)
#define RAW_DATA_SIZE               (1296)
#define RAWDATA_ROW					(36)
#define RAWDATA_COLUMN				(18)
#endif

#define DEMO_RAWDATA_SIZE           (DEMO_DATA_SIZE + RAW_DATA_SIZE)
#define FW_SIZE                     (64 * 1024)

extern int g_rawdata_row;
extern int g_rawdata_col;

#define BIT0   (1<<0)		/* 0x01 */
#define BIT1   (1<<1)		/* 0x02 */
#define BIT2   (1<<2)		/* 0x04 */
#define BIT3   (1<<3)		/* 0x08 */
#define BIT4   (1<<4)		/* 0x10 */
#define BIT5   (1<<5)		/* 0x20 */
#define BIT6   (1<<6)		/* 0x40 */
#define BIT7   (1<<7)		/* 0x80 */

/*
 * GESTURE WAKEUP
 */
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
#define GESTURE_NULL               0
#define GESTURE_DOUBLE_CLICK       1
#define GESTURE_UP                 2
#define GESTURE_DOWN               3
#define GESTURE_LEFT               4
#define GESTURE_RIGHT              5
#define GESTURE_C                  6
#define GESTURE_E                  7
#define GESTURE_M                  8
#define GESTURE_N                  9
#define GESTURE_O                  10
#define GESTURE_S                  11
#define GESTURE_V                  12
#define GESTURE_W                  13
#define GESTURE_Z                  14

#define GESTURE_MAX                15

#define GESTURE_KEY   KEY_POWER
#endif

#ifdef CONFIG_ENABLE_FW_RAWDATA
enum FW_MODE {
	DEMO,
	RAWDATA,
	DEMO_RAWDATA
};
#endif

enum fw_event_type {
	FW_UPDATE = 0,
	FW_READ_REG,
	FW_WRITE_REG,
	FW_READ_OPEN,
	FW_READ_SHORT,
	FW_EDGE_0,
	FW_EDGE_90,
	FW_CHARGER_PLUG,
	FW_CHARGER_UNPLUG,
	FW_HEADSET_PLUG,
	FW_HEADSET_UNPLUG,
	FW_READ_RAWDATA,
	FW_READ_DIFFDATA,
	FW_READ_NOISE,
	FW_GESTURE_ENABLE,
 	FW_GESTURE_DISABLE,
 	FW_REPORT_RATE_60,
 	FW_REPORT_RATE_90,
};

struct gcore_dev {
	struct input_dev *input_device;
	struct task_struct *thread;
	u8 *touch_data;

#if defined(CONFIG_TOUCH_DRIVER_INTERFACE_I2C)
	struct i2c_client *bus_device;
#elif defined(CONFIG_TOUCH_DRIVER_INTERFACE_SPI)
	struct spi_device *bus_device;
#endif

	unsigned int touch_irq;
	spinlock_t irq_flag_lock;
	int irq_flag;
	int tpd_flag;

	struct mutex transfer_lock;
	wait_queue_head_t wait;

	int irq_gpio;
	int rst_gpio;
	void (*rst_output)(int rst, int level);
	void (*irq_enable)(struct gcore_dev *gdev);
	void (*irq_disable)(struct gcore_dev *gdev);

#ifdef CONFIG_ENABLE_FW_RAWDATA
	enum FW_MODE fw_mode;
	wait_queue_head_t usr_wait;
	bool usr_read;
	bool data_ready;
#endif

	/* for driver request event and fw reply with interrupt */
	enum fw_event_type fw_event;
	u8 *firmware;
	int fw_xfer;
	struct notifier_block charger_notifier;
	struct workqueue_struct *fwu_workqueue;
	struct workqueue_struct *gtp_workqueue;
	struct delayed_work fwu_work;
#ifdef GCORE_WDT_RECOVERY_ENABLE
	struct delayed_work wdt_work;
	int ts_stat;
	bool tp_esd_check_error;
#endif
	struct delayed_work charger_work;

	bool tp_fw_update;
	bool tp_self_test;
	bool tp_suspend;
	bool headset_mode;
	bool charger_mode;
	int display_rotation;
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	bool gesture_wakeup_en;
#endif
#ifdef CONFIG_GESTURE_SPECIAL_INT
	int ges_irq_gpio;
	unsigned int ges_irq;
	bool ges_irq_en;
#endif

#ifdef CONFIG_DRM_MSM
	struct notifier_block drm_notifier;
#elif defined(CONFIG_FB)
	struct notifier_block fb_notifier;
#endif

};

enum exp_fn {
	GCORE_FW_UPDATE = 0,
	GCORE_FS_INTERFACE,
	GCORE_MP_TEST,
};

struct gcore_exp_fn {
	enum exp_fn fn_type;
	bool wait_int;
	bool event_flag;
	int (*init)(struct gcore_dev *);
	void (*remove)(struct gcore_dev *);
	struct list_head link;
};

struct gcore_exp_fn_data {
	bool initialized;
	bool fn_inited;
	struct list_head list;
	struct gcore_dev *gdev;
};

/*
 * Declaration
 */

extern s32 gcore_bus_read(u8 *buffer, s32 len);
extern s32 gcore_bus_write(const u8 *buffer, s32 len);
extern int gcore_touch_bus_init(void);
extern void gcore_touch_bus_exit(void);
extern void gcore_new_function_register(struct gcore_exp_fn *exp_fn);
extern void gcore_new_function_unregister(struct gcore_exp_fn *exp_fn);
extern s32 gcore_spi_write_then_read(u8 *txbuf, s32 n_tx, u8 *rxbuf, s32 n_rx);

extern int gcore_read_fw_version(u8 *version, int length);
extern int gcore_read_fw_version_retry(u8 *version, int length);
extern int gcore_auto_update_hostdownload_proc(void *fw_buf);
extern int gcore_auto_update_flashdownload_proc(void *fw_buf);
extern int gcore_flashdownload_fspi_proc(void *fw_buf);
extern int gcore_fw_mode_set_proc2(u8 mode);
extern s32 gcore_fw_read_reg(u32 addr, u8 *buffer, s32 len);
extern s32 gcore_fw_write_reg(u32 addr, u8 *buffer, s32 len);
extern int gcore_create_attribute(struct device *dev);
extern int gcore_idm_read_id(u8 *id, int length);
extern int gcore_update_hostdownload_idm2(u8 *fw_buf);
extern int gcore_upgrade_soft_reset(void);

extern int gcore_enter_idm_mode(void);
extern int gcore_exit_idm_mode(void);
extern s32 gcore_idm_read_reg(u32 addr, u8 *buffer, s32 len);
extern s32 gcore_idm_write_reg(u32 addr, u8 *buffer, s32 len);

extern struct gcore_exp_fn fw_update_fn;
extern struct gcore_exp_fn fs_interf_fn;
extern struct gcore_exp_fn mp_test_fn;
extern struct gcore_exp_fn_data fn_data;

extern unsigned char gcore_mp_FW[];
extern void gcore_request_firmware_update_work(struct work_struct *work);
extern int gcore_mp_firmware_update(void);
extern void gcore_touch_release_all_point(struct input_dev *dev);
#ifdef CONFIG_DRM_MSM
extern int gcore_ts_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#elif defined(CONFIG_FB)
extern int gcore_ts_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#endif

extern int gcore_start_mp_test(void);
extern int gcore_upgrade_soft_reset(void);
int gcore_fw_event_notify(enum fw_event_type event);
int gcore_idm_close_tp(void);
s32 gcore_fw_read_rawdata(u8 *buffer, s32 len);
s32 gcore_fw_read_diffdata(u8 *buffer, s32 len);
#endif /* GCORE_TPD_COMMON_H_  */
