#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include "fpc1020.h"

#define FPC_DRIVER_VERSION	"v2023-03-23"
#define FPC_MODULE_NAME "fpc1020"
#define FPC_RESET_LOW_US 5000
#define FPC_RESET_HIGH1_US 100
#define FPC_RESET_HIGH2_US 5000

#define FPC_TTW_HOLD_TIME 2000
#define SUPPLY_1V8	1800000UL
#define SUPPLY_3V3	3300000UL
#define SUPPLY_TX_MIN	SUPPLY_3V3
#define SUPPLY_TX_MAX	SUPPLY_3V3

static irqreturn_t fpc_irq_handler(int irq, void *handle);

static int hw_reset(struct  fpc_data *fpc)
{
	int irq_gpio;

	fpc_debug(INFO_LOG, "[3]%s enter!\n", __func__);

	if (gpio_is_valid(fpc->rst_gpio)) {
		fpc_debug(INFO_LOG, "reset begin.\n");
		gpio_direction_output(fpc->rst_gpio, 1);
		usleep_range(FPC_RESET_HIGH1_US, FPC_RESET_HIGH1_US + 100);
		fpc_debug(INFO_LOG, "reset start.\n");

		gpio_direction_output(fpc->rst_gpio, 0);
		usleep_range(FPC_RESET_LOW_US, FPC_RESET_LOW_US + 100);
		fpc_debug(INFO_LOG, "reset end.\n");

		gpio_direction_output(fpc->rst_gpio, 1);
		usleep_range(FPC_RESET_HIGH2_US, FPC_RESET_HIGH2_US + 100);
		fpc_debug(INFO_LOG, "reset finish.\n");

	}

	irq_gpio = gpio_get_value(fpc->irq_gpio);
	fpc_debug(INFO_LOG, "IRQ after reset %d\n", irq_gpio);

	return 0;
}

static ssize_t hw_reset_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct  fpc_data *fpc = dev_get_drvdata(dev);

	fpc_debug(INFO_LOG, "%s, %s\n", __func__, buf);

	if (!strncmp(buf, "reset", strlen("reset"))) {
		rc = hw_reset(fpc);
		return rc ? rc : count;
	}

	return -EINVAL;
}
static DEVICE_ATTR(hw_reset, S_IWUSR, NULL, hw_reset_set);

/**
 * sysfs node for controlling whether the driver is allowed
 * to wake up the platform on interrupt.
 */
static ssize_t wakeup_enable_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct  fpc_data *fpc = dev_get_drvdata(dev);
	ssize_t ret = count;

	fpc_debug(INFO_LOG, "%s, %s\n", __func__, buf);

	mutex_lock(&fpc->mutex);
	if (!strncmp(buf, "enable", strlen("enable")))
		fpc->wakeup_enabled = true;
	else if (!strncmp(buf, "disable", strlen("disable"))) {
		fpc->wakeup_enabled = false;
		fpc->nbr_irqs_received = 0;
	}
	else
		ret = -EINVAL;
	mutex_unlock(&fpc->mutex);

	return ret;
}
static DEVICE_ATTR(wakeup_enable, S_IWUSR, NULL, wakeup_enable_set);

/**
 * sysfs node for controlling the wakelock.
 */
static ssize_t handle_wakelock_cmd(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct  fpc_data *fpc = dev_get_drvdata(dev);
	ssize_t ret = count;

	mutex_lock(&fpc->mutex);
	if (!strncmp(buf, RELEASE_WAKELOCK_W_V, min(count,
		strlen(RELEASE_WAKELOCK_W_V)))) {
		if (fpc->nbr_irqs_received_counter_start ==
				fpc->nbr_irqs_received) {
			fpc_debug(INFO_LOG, "%s, __pm_relax1\n", __func__);
			__pm_relax(fpc->ttw_wl);
		} else {
			fpc_debug(INFO_LOG, "Ignore releasing of wakelock %d != %d",
				fpc->nbr_irqs_received_counter_start,
				fpc->nbr_irqs_received);
		}
	} else if (!strncmp(buf,RELEASE_WAKELOCK, min(count,
					strlen(RELEASE_WAKELOCK)))) {
		fpc_debug(INFO_LOG, "%s, __pm_relax2\n", __func__);
		__pm_relax(fpc->ttw_wl);
	} else if (!strncmp(buf, START_IRQS_RECEIVED_CNT,
			min(count, strlen(START_IRQS_RECEIVED_CNT)))) {
		fpc_debug(INFO_LOG, "%s, nbr_irqs_received_counter_start %d\n", __func__, fpc->nbr_irqs_received);
		fpc->nbr_irqs_received_counter_start = fpc->nbr_irqs_received;
	} else
		ret = -EINVAL;
	mutex_unlock(&fpc->mutex);

	return ret;
}
static DEVICE_ATTR(handle_wakelock, S_IWUSR, NULL, handle_wakelock_cmd);

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device *device,
			struct device_attribute *attribute,
			char* buffer)
{
	struct fpc_data *fpc = dev_get_drvdata(device);
	int irq = gpio_get_value(fpc->irq_gpio);

	fpc_debug(ERR_LOG, "%s, irq %d\n", __func__, irq);

	return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}

/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device *device,
			struct device_attribute *attribute,
			const char *buffer, size_t count)
{
	/*struct fpc_data *fpc = dev_get_drvdata(device);*/

	fpc_debug(ERR_LOG, "%s\n", __func__);

	return count;
}

static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);

static ssize_t clk_enable_set(struct device *device,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/*struct fpc_data *fpc = dev_get_drvdata(device);*/
	/*
	if (!fpc->hwabs->clk_enable_set)
		return count;

	return fpc->hwabs->clk_enable_set(fpc, buf, count);
	*/
	return 0;
}

static DEVICE_ATTR(clk_enable, S_IWUSR, NULL, clk_enable_set);

static int fpc_wakeup_init(struct device *dev)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);

	fpc_debug(DEBUG_LOG, "[5]>>>%s enter!\n", __func__);

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	fpc->ttw_wl = wakeup_source_register(dev, "fpc_ttw_wl");
#else
	wakeup_source_init(fpc->ttw_wl, "fpc_ttw_wl");
#endif
	if (!fpc->ttw_wl) {
		fpc_debug(ERR_LOG, "could not create wakeup_source\n");
		return GENERIC_ERR;
	}

	fpc_debug(INFO_LOG, ">>>%s:fpc_wakeup_init done\n", __func__);

	fpc_debug(DEBUG_LOG, ">>>%s exit!\n", __func__);

	return GENERIC_OK;
}

static int fpc_irq_init(struct device *dev)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	int rc = 0;
	int irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	/*int irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND; */
	/* IRQF_NO_SUSPEND flag will cause can not unlock when suspend
	See Documentation/power/suspend-and-interrupts.txt*/

	fpc_debug(INFO_LOG, ">>>%s enter!\n", __func__);
	fpc_debug(INFO_LOG, ">>>%s:irq flags:0x%08x\n", __func__, irqf);

	fpc->irq_num = gpio_to_irq(fpc->irq_gpio);
	if (fpc->irq_num == 0) {
		fpc_debug(ERR_LOG, ">>>%s:gpio_to_irq failed\n", __func__);
		return -EINVAL;
	} else {
		fpc_debug(INFO_LOG, ">>>%s:gpio_to_irq success\n", __func__);
	}
	
	rc = devm_request_threaded_irq(fpc->dev, fpc->irq_num,
			NULL, fpc_irq_handler, irqf,
			dev_name(fpc->dev), fpc);
	if (rc) {
		fpc_debug(ERR_LOG, ">>>request irq %d failed\n", fpc->irq_num);
		return -EINVAL;
	}

	fpc_debug(INFO_LOG, ">>>requested irq %d success\n", fpc->irq_num);

	/* Request that the interrupt should be wakeable */
	enable_irq_wake(fpc->irq_num);

	return rc;
}

static int fpc_power_enable(struct fpc_data *fpc, bool enable)
{
	int ret = 0;

	fpc_debug(INFO_LOG, "[2]%s enter!\n", __func__);

	if (enable) {
		if ((fpc->power_type == 1) && (fpc->fp_reg != NULL)) {
			ret = regulator_enable(fpc->fp_reg);
			if (ret != 0) {
				fpc_debug(ERR_LOG, ">>>%s:regulator_enable failed, ret=%d\n", __func__, ret);
				return -EINVAL;
			} else {
				fpc_debug(ERR_LOG, ">>>%s:regulator === power on ===\n", __func__);
			}
		} else if (gpio_is_valid(fpc->pwr_gpio)) {
			gpio_direction_output(fpc->pwr_gpio, 1);
			fpc_debug(INFO_LOG, ">>>%s:gpio === power on ===\n", __func__);
		} else {
			fpc_debug(ERR_LOG, ">>>%s: power on found power source failed\n", __func__);
			return -EINVAL;
		}
	} else {
		if ((fpc->power_type == 1) && (fpc->fp_reg != NULL)) {
			ret = regulator_disable(fpc->fp_reg);
			if (ret != 0) {
				fpc_debug(ERR_LOG, ">>>%s:regulator_disable failed, ret=%d\n", __func__, ret);
				return -EINVAL;
			} else {
				fpc_debug(INFO_LOG, "<<<%s:regulator === power down ===\n", __func__);
			}
		} else if (gpio_is_valid(fpc->pwr_gpio)) {
			gpio_set_value(fpc->pwr_gpio, 0);
			fpc_debug(INFO_LOG, "<<<%s:gpio === power down ===\n", __func__);
		} else {
			fpc_debug(ERR_LOG, "<<<%s: power down found power source failed\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static int fpc_irq_gpio_enable(struct fpc_data *fpc, bool enable)
{
	fpc_debug(INFO_LOG, "[4]%s enter!\n", __func__);

	if (enable) {
		if (gpio_is_valid(fpc->irq_gpio)) {
			fpc_debug(INFO_LOG, ">>>%s:gpio irq config input\n", __func__);
			gpio_direction_input(fpc->irq_gpio);
		}
	} else {
		if (fpc->irq_num != 0){
			disable_irq_nosync(fpc->irq_num);
			devm_free_irq(fpc->dev, fpc->irq_num, fpc);
			fpc->irq_num = 0;
			fpc_debug(INFO_LOG, "<<<%s:disable_irq_nosync and devm_free_irq\n", __func__);
		}

		gpio_direction_output(fpc->irq_gpio, 0);
	}

	return 0;
}

static int fpc_gpio_request(struct device *dev, bool request)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	int ret = 0;

	fpc_debug(INFO_LOG, "[1]%s enter!\n", __func__);

	if (request) {
		if (gpio_is_valid(fpc->irq_gpio)) {
			ret = devm_gpio_request(dev, fpc->irq_gpio, "fpc_irq");
			if (ret) {
				fpc_debug(ERR_LOG, ">>>%s:request fpc_irq failed, ret=%d\n", __func__, ret);
				return -EINVAL;
			} else {
				fpc_debug(INFO_LOG, ">>>%s:request fpc_irq success\n", __func__);
			}
		}

		if (gpio_is_valid(fpc->rst_gpio)) {
			ret = devm_gpio_request(dev, fpc->rst_gpio, "fpc_rst");
			if (ret) {
				fpc_debug(ERR_LOG, ">>>%s:request fpc_rst failed, ret=%d\n", __func__, ret);
				return -EINVAL;
			} else {
				fpc_debug(INFO_LOG, ">>>%s:request fpc_rst success\n", __func__);
			}
		}

		if (fpc->power_type == 1) {
			fpc->fp_reg = devm_regulator_get(dev, "vdd");
			if (IS_ERR_OR_NULL(fpc->fp_reg)) {
				fpc_debug(ERR_LOG, ">>>%s:get regulator failed\n", __func__);
				return -EINVAL;
			} else {
				fpc_debug(INFO_LOG, ">>>%s:get regulator success\n", __func__);
			}

			ret = regulator_set_voltage(fpc->fp_reg, fpc->power_voltage, fpc->power_voltage);
			if (ret) {
				fpc_debug(ERR_LOG, ">>>%s:regulator_set_voltage failed, ret=%d\n", __func__, ret);
				return -EINVAL;
			} else {
				fpc_debug(INFO_LOG, ">>>%s:regulator_set_voltage success, power_voltage %d\n", __func__, fpc->power_voltage);
			}
		} else {
			if (gpio_is_valid(fpc->pwr_gpio)) {
				ret = devm_gpio_request(dev, fpc->pwr_gpio, "fpc_vdd");
				if (ret) {
					fpc_debug(ERR_LOG, ">>>%s:request fpc_vdd failed, ret=%d\n", __func__, ret);
					return -EINVAL;
				} else {
					fpc_debug(INFO_LOG, ">>>%s:request fpc_vdd success\n", __func__);
				}
			}
		}
	}else {
		if (gpio_is_valid(fpc->irq_gpio)) {
			devm_gpio_free(dev, fpc->irq_gpio);
			fpc_debug(INFO_LOG, "<<<%s:free irq_gpio success\n", __func__);
		}

		if (gpio_is_valid(fpc->rst_gpio)) {
			devm_gpio_free(dev, fpc->rst_gpio);
			fpc_debug(INFO_LOG, "<<<%s:free rst_gpio success\n", __func__);
		}

		if (fpc->power_type == 1) {
			if (!IS_ERR_OR_NULL(fpc->fp_reg)) {
				regulator_put(fpc->fp_reg);
				fpc->fp_reg = NULL;
				fpc_debug(INFO_LOG, "<<<%s:regulator put success\n", __func__);
			}
		} else {
			if (gpio_is_valid(fpc->pwr_gpio)) {
				devm_gpio_free(dev, fpc->pwr_gpio);
				fpc_debug(INFO_LOG, "<<<%s:free pwr_gpio success\n", __func__);
			}
		}
	}

	return ret;
}


static int fpc_gpio_init(struct device *dev)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	int ret = 0;

	fpc_debug(INFO_LOG, ">>>%s enter!\n", __func__);

	ret = fpc_gpio_request(dev, true);
	if (ret) {
		fpc_debug(INFO_LOG, ">>> fpc_gpio_request failed\n", __func__, ret);
		goto exit_loop;
	}

	ret = fpc_power_enable(fpc, true);
	if (ret) {
		fpc_debug(INFO_LOG, ">>> fpc_power_enable failed\n", __func__, ret);
		goto exit_loop;
	}

	ret = hw_reset(fpc);
	if (ret) {
		fpc_debug(INFO_LOG, ">>> hw_reset failed\n", __func__, ret);
		goto exit_loop;
	}

	ret = fpc_irq_gpio_enable(fpc, true);
	if (ret) {
		fpc_debug(INFO_LOG, ">>> fpc_irq_gpio_enable failed\n", __func__, ret);
		goto exit_loop;
	}

exit_loop:
	fpc_debug(INFO_LOG, ">>>%s exit, ret=%d\n", __func__, ret);

	return ret;
}

static int fpc_gpio_exit(struct device *dev)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);
	int ret = 0;

	fpc_debug(INFO_LOG, "<<<%s enter!\n", __func__);

	ret = fpc_irq_gpio_enable(fpc, false);
	if (ret) {
		fpc_debug(INFO_LOG, "<<< fpc_irq_gpio_enable failed\n", __func__, ret);
		goto exit_loop;
	}

	ret = fpc_power_enable(fpc, false);
	if (ret) {
		fpc_debug(INFO_LOG, "<<< fpc_power_enable failed\n", __func__, ret);
		goto exit_loop;
	}

	ret = fpc_gpio_request(dev, false);
	if (ret) {
		fpc_debug(INFO_LOG, "<<< fpc_gpio_request failed\n", __func__, ret);
		goto exit_loop;
	}

exit_loop:
	fpc_debug(INFO_LOG, "<<<%s exit, ret=%d\n", __func__, ret);

	return ret;
}

static int fpc_driver_init(struct device *dev)
{
	int i = 0;
	int (*fpc_p[3])(struct device *dev) = {fpc_gpio_init, fpc_irq_init, fpc_wakeup_init};

	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

	for (i = 0; i < 3; i++) {
		if(fpc_p[i](dev) != GENERIC_OK)
			return GENERIC_ERR;
	}

	fpc_debug(INFO_LOG, "%s exit!\n", __func__);

	return GENERIC_OK;
}

static ssize_t compatible_all_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct fpc_data *fpc = dev_get_drvdata(dev);

	fpc_debug(INFO_LOG, "%s enter %s, irq_num %d!\n", __func__, buf, fpc->irq_num);

	if (0 == strncmp(buf, "enable", strlen("enable")) && fpc->irq_num == 0) {
		fpc_debug(INFO_LOG, "%s:fpc_driver_init\n", __func__);
		if (fpc_driver_init(dev) != GENERIC_OK) {
			return GENERIC_ERR;
		}
	} else if (0 == strncmp(buf, "disable", strlen("disable")) && fpc->irq_num != 0) {
		fpc_debug(INFO_LOG, "%s:fpc_gpio_exit\n", __func__);
		fpc_gpio_exit(dev);
	}
	(void)attr;
	fpc_debug(INFO_LOG, "%s exit!\n", __func__);
	return count;
}

static DEVICE_ATTR(compatible_all, S_IWUSR, NULL, compatible_all_set);

static struct attribute *fpc_attributes[] = {
	&dev_attr_hw_reset.attr,
	&dev_attr_wakeup_enable.attr,
	&dev_attr_handle_wakelock.attr,
	&dev_attr_clk_enable.attr,
	&dev_attr_irq.attr,
	&dev_attr_compatible_all.attr,
	NULL
};

static struct attribute_group const fpc_attribute_group = {
	.attrs = fpc_attributes,
};

static irqreturn_t fpc_irq_handler(int irq, void *handle)
{
	struct fpc_data *fpc = handle;

	fpc_debug(ERR_LOG, "%s\n", __func__);

/*
	if (fpc->hwabs->irq_handler)
		fpc->hwabs->irq_handler(irq, fpc);
*/

	/* Make sure 'wakeup_enabled' is updated before using it
	** since this is interrupt context (other thread...) */
	mutex_lock(&fpc->mutex);
	if (fpc->wakeup_enabled) {
		__pm_wakeup_event(fpc->ttw_wl, FPC_TTW_HOLD_TIME);
		fpc->nbr_irqs_received++;
	}
	mutex_unlock(&fpc->mutex);

	sysfs_notify(&fpc->dev->kobj, NULL, dev_attr_irq.attr.name);

	return IRQ_HANDLED;
}

#if defined(USE_SPI_BUS)
int fpc_probe(struct spi_device *pldev)
#else
int fpc_probe(struct platform_device *pldev)
#endif
{
	struct device *dev = &pldev->dev;
	struct device_node *node = dev->of_node;
	struct fpc_data *fpc = NULL;
	struct fpc_gpio_info *fpc_gpio_ops = NULL;
	int rc = 0;

	fpc_debug(INFO_LOG, "%s line %d\n", __func__, __LINE__);

	fpc = devm_kzalloc(dev, sizeof(*fpc), GFP_KERNEL);
	if (!fpc) {
		fpc_debug(ERR_LOG, "failed to allocate memory for struct fpc_data\n");
		rc = -ENOMEM;
		goto exit;
	}

	fpc->dev = dev;
	dev_set_drvdata(dev, fpc);
	fpc->pldev = pldev;
	fpc->hwabs = fpc_gpio_ops;

	if (!node) {
		fpc_debug(ERR_LOG, "no of node found\n");
		rc = -EINVAL;
		goto exit;
	}

	fpc->irq_gpio = of_get_named_gpio(node, "fpc_irq", 0);
	if (!gpio_is_valid(fpc->irq_gpio)) {
		fpc_debug(ERR_LOG, "Requesting GPIO for IRQ failed with %d.\n", rc);
		goto exit;
	}

	fpc->rst_gpio = of_get_named_gpio(node, "fpc_rst", 0);
	if (!gpio_is_valid(fpc->rst_gpio)) {
		fpc_debug(ERR_LOG, "Requesting GPIO for RST failed with %d.\n", rc);
		goto exit;
	}

	fpc_debug(INFO_LOG, "Using GPIO#%d as IRQ.\n", fpc->irq_gpio);
	fpc_debug(INFO_LOG, "Using GPIO#%d as RST.\n", fpc->rst_gpio);

	/*--------------------fpc_pwr--------------------*/
	rc = of_property_read_u32(node, "power-type", &fpc->power_type);
	if (rc < 0) {
		fpc_debug(ERR_LOG, "%s:Power type get failed from dts, ret=%d\n", __func__, rc);
	}

	fpc_debug(INFO_LOG, "%s:power type[%d]\n", __func__, fpc->power_type);

	if (fpc->power_type == 1) {
		/* get power voltage from dts config */
		rc = of_property_read_u32(node, "power-voltage", &fpc->power_voltage);
		if (rc < 0) {
			fpc_debug(ERR_LOG, "Power voltage get failed from dts, ret=%d\n", rc);
		}

		fpc_debug(INFO_LOG, "%s:Power voltage[%d]\n", __func__, fpc->power_voltage);
	} else {
		fpc->pwr_gpio = of_get_named_gpio(node, "fpc_vdd", 0);
		if (!gpio_is_valid(fpc->pwr_gpio)) {
			fpc_debug(ERR_LOG, "%s:get name fpc_vdd failed\n", __func__);
			goto exit;
		}

		fpc_debug(INFO_LOG, "%s:pwr_gpio[%d]\n", __func__, fpc->pwr_gpio);
	}

	fpc->wakeup_enabled = false;
	mutex_init(&fpc->mutex);

	rc = sysfs_create_group(&dev->kobj, &fpc_attribute_group);
	if (rc) {
		fpc_debug(ERR_LOG, "could not create sysfs\n");
		goto exit;
	}

	fpc_debug(INFO_LOG, "%s: ok\n", __func__);
exit:
	return rc;
}

#if defined(USE_SPI_BUS)
int fpc_remove(struct spi_device *pldev)
#else
int fpc_remove(struct platform_device *pldev)
#endif
{
	struct  fpc_data *fpc = dev_get_drvdata(&pldev->dev);

	sysfs_remove_group(&pldev->dev.kobj, &fpc_attribute_group);
	mutex_destroy(&fpc->mutex);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	wakeup_source_unregister(fpc->ttw_wl);
#else
	wakeup_source_trash(fpc->ttw_wl);
#endif
	fpc_debug(INFO_LOG, "%s\n", __func__);

	return 0;
}

static struct of_device_id fpc_of_match[] = {
	{ .compatible = "fpc,fpc1020", },
	{}
};


#if defined(USE_SPI_BUS)
static struct spi_driver fpc_spi_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.bus = &spi_bus_type,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_probe,
	.remove = fpc_remove,
};
#else
static struct platform_driver fpc_plat_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_probe,
	.remove = fpc_remove,
};
#endif

int fpc_init(void)
{
	fpc_debug(INFO_LOG, "%s enter! driver version:%s\n", __func__, FPC_DRIVER_VERSION);

#if defined(USE_SPI_BUS)
	fpc_debug(INFO_LOG, "%s:spi_register_driver", __func__);
	return spi_register_driver(&fpc_spi_driver);
#else
	fpc_debug(INFO_LOG, "%s:platform_driver_register", __func__);
	return platform_driver_register(&fpc_plat_driver);
#endif

	return 0;
}

void fpc_exit(void)
{
	fpc_debug(INFO_LOG, "%s enter!\n", __func__);

#if defined(USE_SPI_BUS)
	fpc_debug(INFO_LOG, "%s:spi_unregister_driver", __func__);
	spi_unregister_driver(&fpc_spi_driver);
#else
	fpc_debug(INFO_LOG, "%s:platform_driver_unregister", __func__);
	platform_driver_unregister(&fpc_plat_driver);
#endif
}

/*module_init(fpc_init);
module_exit(fpc_exit);*/

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("sheldon <sheldon.xie@fingerprints.com>");
MODULE_DESCRIPTION("fpc fingerprint sensor device driver");
