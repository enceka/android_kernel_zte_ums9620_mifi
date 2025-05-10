#ifndef _FPC_1020_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#define RELEASE_WAKELOCK_W_V "release_wakelock_with_verification"
#define RELEASE_WAKELOCK "release_wakelock"
#define START_IRQS_RECEIVED_CNT "start_irqs_received_counter"

#define GENERIC_OK 0
#define GENERIC_ERR -1
#define FPC_GPIO_NUM 3

struct fpc_gpio_info;

struct fpc_data {
	struct device *dev;
	struct platform_device *pldev;

	int irq_gpio;
	int rst_gpio;
	int pwr_gpio;
	int irq_num;

	/* 0 fingerprint use system gpio control  power, 1 pmic power */
	int power_type;
	int power_voltage;
	struct regulator *fp_reg;

	int nbr_irqs_received;
	int nbr_irqs_received_counter_start;

	bool wakeup_enabled;

	struct wakeup_source *ttw_wl;

	bool power_enabled;
	bool use_regulator_for_bezel;
	const struct fpc_gpio_info *hwabs;

	struct mutex mutex;
	
};

typedef enum {
	ERR_LOG = 0,
	WARN_LOG,
	INFO_LOG,
	DEBUG_LOG,
	ALL_LOG,
} fpc1020_debug_level_t;

static fpc1020_debug_level_t fpc1020_debug_level = INFO_LOG;

#define fpc_debug(level, fmt, args...) do { \
			if (fpc1020_debug_level >= level) {\
				pr_err("[fpc_info] " fmt, ##args); \
			} \
		} while (0)

struct fpc_gpio_info {
	int (*init)(struct fpc_data *fpc);
	int (*configure)(struct fpc_data *fpc, int *irq_num, int *trigger_flags);
	int (*get_val)(unsigned gpio);
	void (*set_val)(unsigned gpio, int val);
	ssize_t (*clk_enable_set)(struct fpc_data *fpc, const char *buf, size_t count);
	void (*irq_handler)(int irq, struct fpc_data *fpc);
	void *priv;
};

#endif
