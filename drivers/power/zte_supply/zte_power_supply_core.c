// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/thermal.h>
#include <linux/zte_power_supply.h>
#define zte_to_power_supply(device) container_of(device, struct zte_power_supply, dev)

/* exported for the APM Power driver, APM emulation */
struct class *zte_power_supply_class;
EXPORT_SYMBOL_GPL(zte_power_supply_class);


static struct device_type zte_power_supply_dev_type;

struct match_device_node_array_param {
	struct device_node *parent_of_node;
	struct zte_power_supply **psy;
	ssize_t psy_size;
	ssize_t psy_count;
};

#define POWER_SUPPLY_DEFERRED_REGISTER_TIME	msecs_to_jiffies(10)

static int find_supply_from_node_result;

static bool __zte_power_supply_is_supplied_by(const struct zte_power_supply *supplier,
					 struct zte_power_supply *supply)
{
	int i;

	if (!supply->supplied_from && !supplier->supplied_to)
		return false;

	/* Support both supplied_to and supplied_from modes */
	if (supply->supplied_from) {
		if (!supplier->desc->name)
			return false;
		for (i = 0; i < supply->num_supplies; i++)
			if (!strcmp(supplier->desc->name, supply->supplied_from[i]))
				return true;
	} else {
		if (!supply->desc->name)
			return false;
		for (i = 0; i < supplier->num_supplicants; i++)
			if (!strcmp(supplier->supplied_to[i], supply->desc->name))
				return true;
	}

	return false;
}

static int __zte_power_supply_changed_work(struct device *dev,const void *data)
{
	const struct zte_power_supply *psy = data;
	struct zte_power_supply *pst = dev_get_drvdata(dev);

	dev_info(dev, "%s, line:%d !\n", __func__, __LINE__);

	if (__zte_power_supply_is_supplied_by(psy, pst)) {
		if (pst->desc->external_power_changed)
			pst->desc->external_power_changed(pst);
	}

	return 0;
}

static void zte_power_supply_changed_work(struct work_struct *work)
{
	unsigned long flags;
	struct zte_power_supply *psy = container_of(work, struct zte_power_supply,
						changed_work);

	dev_info(&psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	/*
	 * Check 'changed' here to avoid issues due to race between
	 * power_supply_changed() and this routine. In worst case
	 * power_supply_changed() can be called again just before we take above
	 * lock. During the first call of this routine we will mark 'changed' as
	 * false and it will stay false for the next call as well.
	 */
	if (likely(psy->changed)) {
		psy->changed = false;
		spin_unlock_irqrestore(&psy->changed_lock, flags);
		class_find_device(zte_power_supply_class, NULL, psy,
				      __zte_power_supply_changed_work);
		kobject_uevent(&psy->dev.kobj, KOBJ_CHANGE);
		spin_lock_irqsave(&psy->changed_lock, flags);
	}

	/*
	 * Hold the wakeup_source until all events are processed.
	 * power_supply_changed() might have called again and have set 'changed'
	 * to true.
	 */
	if (likely(!psy->changed))
		pm_relax(&psy->dev);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
}

void zte_power_supply_changed(struct zte_power_supply *psy)
{
	unsigned long flags;

	dev_info(&psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	psy->changed = true;
	pm_stay_awake(&psy->dev);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
	schedule_work(&psy->changed_work);
}
EXPORT_SYMBOL_GPL(zte_power_supply_changed);

/*
 * Notify that power supply was registered after parent finished the probing.
 *
 * Often power supply is registered from driver's probe function. However
 * calling power_supply_changed() directly from power_supply_register()
 * would lead to execution of get_property() function provided by the driver
 * too early - before the probe ends.
 *
 * Avoid that by waiting on parent's mutex.
 */
static void zte_power_supply_deferred_register_work(struct work_struct *work)
{
	struct zte_power_supply *psy = container_of(work, struct zte_power_supply,
						deferred_register_work.work);

	if (psy->dev.parent) {
		while (!mutex_trylock(&psy->dev.parent->mutex)) {
			if (psy->removing)
				return;
			msleep(10);
		}
	}

	zte_power_supply_changed(psy);

	if (psy->dev.parent)
		mutex_unlock(&psy->dev.parent->mutex);
}

#ifdef CONFIG_OF
static int __zte_power_supply_populate_supplied_from(struct device *dev,
						const void *data)
{
	const struct zte_power_supply *psy = data;
	struct device_node *np;
	int i = 0;

	do {
		np = of_parse_phandle(psy->of_node, "power-supplies", i++);
		if (!np)
			break;
/*
		if (np == epsy->of_node) {
			dev_info(&psy->dev, "%s: Found supply : %s\n",
				psy->desc->name, epsy->desc->name);
			psy->supplied_from[i-1] = (char *)epsy->desc->name;
			psy->num_supplies++;
			of_node_put(np);
			break;
		}
*/
		of_node_put(np);
	} while (np);

	return 0;
}

static int zte_power_supply_populate_supplied_from(struct zte_power_supply *psy)
{
	class_find_device(zte_power_supply_class, NULL, psy,
				      __zte_power_supply_populate_supplied_from);

	dev_info(&psy->dev, "%s %d\n", __func__, 0);

	return 0;
}

static int __zte_power_supply_find_supply_from_node(struct device *dev,
						const void *data)
{
	const struct device_node *np = data;
	struct zte_power_supply *epsy = dev_get_drvdata(dev);

	/* returning non-zero breaks out of class_find_device loop */
	if (epsy->of_node == np) {
		dev_info(dev, "line:%d find_supply_from_node_result=%d!\n", __LINE__, find_supply_from_node_result);
		find_supply_from_node_result = 1;
		return find_supply_from_node_result;
	}
	dev_info(dev, "line:%d find_supply_from_node_result=%d!\n", __LINE__, find_supply_from_node_result);
	find_supply_from_node_result = 0;
	return find_supply_from_node_result;
}

static int zte_power_supply_find_supply_from_node(struct device_node *supply_node)
{
	int error;

	/*
	 * class_for_each_device() either returns its own errors or values
	 * returned by __power_supply_find_supply_from_node().
	 *
	 * __power_supply_find_supply_from_node() will return 0 (no match)
	 * or 1 (match).
	 *
	 * We return 0 if class_for_each_device() returned 1, -EPROBE_DEFER if
	 * it returned 0, or error as returned by it.
	 */
	class_find_device(zte_power_supply_class, NULL, supply_node,
				       __zte_power_supply_find_supply_from_node);

	error = find_supply_from_node_result;

	return error ? (error == 1 ? 0 : error) : -EPROBE_DEFER;
}

static int zte_power_supply_check_supplies(struct zte_power_supply *psy)
{
	struct device_node *np;
	int cnt = 0;

	/* If there is already a list honor it */
	if (psy->supplied_from && psy->num_supplies > 0)
		return 0;

	/* No device node found, nothing to do */
	if (!psy->of_node)
		return 0;

	do {
		int ret;

		np = of_parse_phandle(psy->of_node, "power-supplies", cnt++);
		if (!np)
			break;

		ret = zte_power_supply_find_supply_from_node(np);
		of_node_put(np);

		if (ret) {
			dev_info(&psy->dev, "Failed to find supply!\n");
			return ret;
		}
	} while (np);

	/* Missing valid "power-supplies" entries */
	if (cnt == 1)
		return 0;

	/* All supplies found, allocate char ** array for filling */
	psy->supplied_from = devm_kzalloc(&psy->dev, sizeof(psy->supplied_from),
					  GFP_KERNEL);
	if (!psy->supplied_from)
		return -ENOMEM;

	*psy->supplied_from = devm_kcalloc(&psy->dev,
					   cnt - 1, sizeof(char *),
					   GFP_KERNEL);
	if (!*psy->supplied_from)
		return -ENOMEM;

	return zte_power_supply_populate_supplied_from(psy);
}
#else
static int zte_power_supply_check_supplies(struct zte_power_supply *psy)
{
	int nval, ret;

	if (!psy->dev.parent)
		return 0;

	nval = device_property_read_string_array(psy->dev.parent,
						 "supplied-from", NULL, 0);
	if (nval <= 0)
		return 0;

	psy->supplied_from = devm_kmalloc_array(&psy->dev, nval,
						sizeof(char *), GFP_KERNEL);
	if (!psy->supplied_from)
		return -ENOMEM;

	ret = device_property_read_string_array(psy->dev.parent,
		"supplied-from", (const char **)psy->supplied_from, nval);
	if (ret < 0)
		return ret;

	psy->num_supplies = nval;

	return 0;
}
#endif

struct psy_am_i_supplied_data {
	struct zte_power_supply *psy;
	unsigned int count;
};

int zte_power_supply_set_battery_charged(struct zte_power_supply *psy)
{
	if (atomic_read(&psy->use_cnt) >= 0 &&
			psy->desc->type == POWER_SUPPLY_TYPE_BATTERY &&
			psy->desc->set_charged) {
		psy->desc->set_charged(psy);
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(zte_power_supply_set_battery_charged);

static int zte_power_supply_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;
	struct zte_power_supply *psy = dev_get_drvdata(dev);

	return strcmp(psy->desc->name, name) == 0;
}

/**
 * power_supply_get_by_name() - Search for a power supply and returns its ref
 * @name: Power supply name to fetch
 *
 * If power supply was found, it increases reference count for the
 * internal power supply's device. The user should power_supply_put()
 * after usage.
 *
 * Return: On success returns a reference to a power supply with
 * matching name equals to @name, a NULL otherwise.
 */
struct zte_power_supply *zte_power_supply_get_by_name(const char *name)
{
	struct zte_power_supply *psy = NULL;
	struct device *dev = class_find_device(zte_power_supply_class, NULL, name,
					zte_power_supply_match_device_by_name);

	if (dev) {
		psy = dev_get_drvdata(dev);
		atomic_inc(&psy->use_cnt);
	}

	return psy;
}
EXPORT_SYMBOL_GPL(zte_power_supply_get_by_name);

/**
 * power_supply_put() - Drop reference obtained with power_supply_get_by_name
 * @psy: Reference to put
 *
 * The reference to power supply should be put before unregistering
 * the power supply.
 */
void zte_power_supply_put(struct zte_power_supply *psy)
{
	might_sleep();

	atomic_dec(&psy->use_cnt);
	put_device(&psy->dev);
}
EXPORT_SYMBOL_GPL(zte_power_supply_put);

#ifdef CONFIG_OF
static int zte_power_supply_match_device_node(struct device *dev, const void *data)
{
	return dev->parent && dev->parent->of_node == data;
}

/**
 * power_supply_get_by_phandle() - Search for a power supply and returns its ref
 * @np: Pointer to device node holding phandle property
 * @property: Name of property holding a power supply name
 *
 * If power supply was found, it increases reference count for the
 * internal power supply's device. The user should power_supply_put()
 * after usage.
 *
 * Return: On success returns a reference to a power supply with
 * matching name equals to value under @property, NULL or ERR_PTR otherwise.
 */
struct zte_power_supply *zte_power_supply_get_by_phandle(struct device_node *np,
							const char *property)
{
	struct device_node *power_supply_np;
	struct zte_power_supply *psy = NULL;
	struct device *dev;

	power_supply_np = of_parse_phandle(np, property, 0);
	if (!power_supply_np)
		return ERR_PTR(-ENODEV);

	dev = class_find_device(zte_power_supply_class, NULL, power_supply_np,
						zte_power_supply_match_device_node);

	of_node_put(power_supply_np);

	if (dev) {
		psy = dev_get_drvdata(dev);
		atomic_inc(&psy->use_cnt);
	}

	return psy;
}
EXPORT_SYMBOL_GPL(zte_power_supply_get_by_phandle);

static void zte_devm_power_supply_put(struct device *dev, void *res)
{
	struct zte_power_supply **psy = res;

	zte_power_supply_put(*psy);
}

/**
 * devm_power_supply_get_by_phandle() - Resource managed version of
 *  power_supply_get_by_phandle()
 * @dev: Pointer to device holding phandle property
 * @property: Name of property holding a power supply phandle
 *
 * Return: On success returns a reference to a power supply with
 * matching name equals to value under @property, NULL or ERR_PTR otherwise.
 */
struct zte_power_supply *zte_devm_power_supply_get_by_phandle(struct device *dev,
						      const char *property)
{
	struct zte_power_supply **ptr, *psy;

	if (!dev->of_node)
		return ERR_PTR(-ENODEV);

	ptr = devres_alloc(zte_devm_power_supply_put, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	psy = zte_power_supply_get_by_phandle(dev->of_node, property);
	if (IS_ERR_OR_NULL(psy)) {
		devres_free(ptr);
	} else {
		*ptr = psy;
		devres_add(dev, ptr);
	}
	return psy;
}
EXPORT_SYMBOL_GPL(zte_devm_power_supply_get_by_phandle);
#endif /* CONFIG_OF */

int zte_power_supply_get_battery_info(struct zte_power_supply *psy,
				  struct power_supply_battery_info *info)
{
	struct power_supply_resistance_temp_table *resist_table;
	struct device_node *battery_np;
	const char *value;
	int err, len, index;
	const __be32 *list;

	info->energy_full_design_uwh         = -EINVAL;
	info->charge_full_design_uah         = -EINVAL;
	info->voltage_min_design_uv          = -EINVAL;
	info->voltage_max_design_uv          = -EINVAL;
	info->precharge_current_ua           = -EINVAL;
	info->charge_term_current_ua         = -EINVAL;
	info->constant_charge_current_max_ua = -EINVAL;
	info->constant_charge_voltage_max_uv = -EINVAL;
	info->factory_internal_resistance_uohm  = -EINVAL;
	info->resist_table = NULL;

	for (index = 0; index < POWER_SUPPLY_OCV_TEMP_MAX; index++) {
		info->ocv_table[index]       = NULL;
		info->ocv_temp[index]        = -EINVAL;
		info->ocv_table_size[index]  = -EINVAL;
	}

	if (!psy->of_node) {
		dev_warn(&psy->dev, "%s currently only supports devicetree\n",
			 __func__);
		return -ENXIO;
	}

	battery_np = of_parse_phandle(psy->of_node, "monitored-battery", 0);
	if (!battery_np)
		return -ENODEV;

	err = of_property_read_string(battery_np, "compatible", &value);
	if (err)
		goto out_put_node;

	if (strcmp("simple-battery", value)) {
		err = -ENODEV;
		goto out_put_node;
	}

	/* The property and field names below must correspond to elements
	 * in enum zte_power_supply_property. For reasoning, see
	 * Documentation/power/power_supply_class.rst.
	 */

	of_property_read_u32(battery_np, "energy-full-design-microwatt-hours",
			     &info->energy_full_design_uwh);
	of_property_read_u32(battery_np, "charge-full-design-microamp-hours",
			     &info->charge_full_design_uah);
	of_property_read_u32(battery_np, "voltage-min-design-microvolt",
			     &info->voltage_min_design_uv);
	of_property_read_u32(battery_np, "voltage-max-design-microvolt",
			     &info->voltage_max_design_uv);
	of_property_read_u32(battery_np, "precharge-current-microamp",
			     &info->precharge_current_ua);
	of_property_read_u32(battery_np, "charge-term-current-microamp",
			     &info->charge_term_current_ua);
	of_property_read_u32(battery_np, "constant-charge-current-max-microamp",
			     &info->constant_charge_current_max_ua);
	of_property_read_u32(battery_np, "constant-charge-voltage-max-microvolt",
			     &info->constant_charge_voltage_max_uv);
	of_property_read_u32(battery_np, "factory-internal-resistance-micro-ohms",
			     &info->factory_internal_resistance_uohm);

	len = of_property_count_u32_elems(battery_np, "ocv-capacity-celsius");
	if (len < 0 && len != -EINVAL) {
		err = len;
		goto out_put_node;
	} else if (len > POWER_SUPPLY_OCV_TEMP_MAX) {
		dev_err(&psy->dev, "Too many temperature values\n");
		err = -EINVAL;
		goto out_put_node;
	} else if (len > 0) {
		of_property_read_u32_array(battery_np, "ocv-capacity-celsius",
					   info->ocv_temp, len);
	}

	for (index = 0; index < len; index++) {
		struct power_supply_battery_ocv_table *table;
		char *propname;
		int i, tab_len, size;

		propname = kasprintf(GFP_KERNEL, "ocv-capacity-table-%d", index);
		list = of_get_property(battery_np, propname, &size);
		if (!list || !size) {
			dev_err(&psy->dev, "failed to get %s\n", propname);
			kfree(propname);
			zte_power_supply_put_battery_info(psy, info);
			err = -EINVAL;
			goto out_put_node;
		}

		kfree(propname);
		tab_len = size / (2 * sizeof(__be32));
		info->ocv_table_size[index] = tab_len;

		table = info->ocv_table[index] =
			devm_kcalloc(&psy->dev, tab_len, sizeof(*table), GFP_KERNEL);
		if (!info->ocv_table[index]) {
			zte_power_supply_put_battery_info(psy, info);
			err = -ENOMEM;
			goto out_put_node;
		}

		for (i = 0; i < tab_len; i++) {
			table[i].ocv = be32_to_cpu(*list);
			list++;
			table[i].capacity = be32_to_cpu(*list);
			list++;
		}
	}

	list = of_get_property(battery_np, "resistance-temp-table", &len);
	if (!list || !len)
		goto out_put_node;

	info->resist_table_size = len / (2 * sizeof(__be32));
	resist_table = info->resist_table = devm_kcalloc(&psy->dev,
							 info->resist_table_size,
							 sizeof(*resist_table),
							 GFP_KERNEL);
	if (!info->resist_table) {
		zte_power_supply_put_battery_info(psy, info);
		err = -ENOMEM;
		goto out_put_node;
	}

	for (index = 0; index < info->resist_table_size; index++) {
		resist_table[index].temp = be32_to_cpu(*list++);
		resist_table[index].resistance = be32_to_cpu(*list++);
	}

out_put_node:
	of_node_put(battery_np);
	return err;
}
EXPORT_SYMBOL_GPL(zte_power_supply_get_battery_info);

void zte_power_supply_put_battery_info(struct zte_power_supply *psy,
				   struct power_supply_battery_info *info)
{
	int i;

	for (i = 0; i < POWER_SUPPLY_OCV_TEMP_MAX; i++) {
		if (info->ocv_table[i])
			devm_kfree(&psy->dev, info->ocv_table[i]);
	}

	if (info->resist_table)
		devm_kfree(&psy->dev, info->resist_table);
}
EXPORT_SYMBOL_GPL(zte_power_supply_put_battery_info);

/**
 * power_supply_temp2resist_simple() - find the battery internal resistance
 * percent
 * @table: Pointer to battery resistance temperature table
 * @table_len: The table length
 * @ocv: Current temperature
 *
 * This helper function is used to look up battery internal resistance percent
 * according to current temperature value from the resistance temperature table,
 * and the table must be ordered descending. Then the actual battery internal
 * resistance = the ideal battery internal resistance * percent / 100.
 *
 * Return: the battery internal resistance percent
 */
int zte_power_supply_temp2resist_simple(struct power_supply_resistance_temp_table *table,
				    int table_len, int temp)
{
	int i, resist;

	for (i = 0; i < table_len; i++)
		if (temp > table[i].temp)
			break;

	if (i > 0 && i < table_len) {
		int tmp;

		tmp = (table[i - 1].resistance - table[i].resistance) *
			(temp - table[i].temp);
		tmp /= table[i - 1].temp - table[i].temp;
		resist = tmp + table[i].resistance;
	} else if (i == 0) {
		resist = table[0].resistance;
	} else {
		resist = table[table_len - 1].resistance;
	}

	return resist;
}
EXPORT_SYMBOL_GPL(zte_power_supply_temp2resist_simple);

/**
 * power_supply_ocv2cap_simple() - find the battery capacity
 * @table: Pointer to battery OCV lookup table
 * @table_len: OCV table length
 * @ocv: Current OCV value
 *
 * This helper function is used to look up battery capacity according to
 * current OCV value from one OCV table, and the OCV table must be ordered
 * descending.
 *
 * Return: the battery capacity.
 */
int zte_power_supply_ocv2cap_simple(struct power_supply_battery_ocv_table *table,
				int table_len, int ocv)
{
	int i, cap, tmp;

	for (i = 0; i < table_len; i++)
		if (ocv > table[i].ocv)
			break;

	if (i > 0 && i < table_len) {
		tmp = (table[i - 1].capacity - table[i].capacity) *
			(ocv - table[i].ocv);
		tmp /= table[i - 1].ocv - table[i].ocv;
		cap = tmp + table[i].capacity;
	} else if (i == 0) {
		cap = table[0].capacity;
	} else {
		cap = table[table_len - 1].capacity;
	}

	return cap;
}
EXPORT_SYMBOL_GPL(zte_power_supply_ocv2cap_simple);

struct power_supply_battery_ocv_table *
zte_power_supply_find_ocv2cap_table(struct power_supply_battery_info *info,
				int temp, int *table_len)
{
	int best_temp_diff = INT_MAX, temp_diff;
	u8 i, best_index = 0;

	if (!info->ocv_table[0])
		return NULL;

	for (i = 0; i < POWER_SUPPLY_OCV_TEMP_MAX; i++) {
		temp_diff = abs(info->ocv_temp[i] - temp);

		if (temp_diff < best_temp_diff) {
			best_temp_diff = temp_diff;
			best_index = i;
		}
	}

	*table_len = info->ocv_table_size[best_index];
	return info->ocv_table[best_index];
}
EXPORT_SYMBOL_GPL(zte_power_supply_find_ocv2cap_table);

int zte_power_supply_batinfo_ocv2cap(struct power_supply_battery_info *info,
				 int ocv, int temp)
{
	struct power_supply_battery_ocv_table *table;
	int table_len;

	table = zte_power_supply_find_ocv2cap_table(info, temp, &table_len);
	if (!table)
		return -EINVAL;

	return zte_power_supply_ocv2cap_simple(table, table_len, ocv);
}
EXPORT_SYMBOL_GPL(zte_power_supply_batinfo_ocv2cap);

int zte_power_supply_get_property(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    union power_supply_propval *val)
{
	if (atomic_read(&psy->use_cnt) <= 0) {
		if (!psy->initialized)
			return -EAGAIN;
		return -ENODEV;
	}

	return psy->desc->get_property(psy, psp, val);
}
EXPORT_SYMBOL_GPL(zte_power_supply_get_property);

int zte_power_supply_set_property(struct zte_power_supply *psy,
			    enum zte_power_supply_property psp,
			    const union power_supply_propval *val)
{
	if (atomic_read(&psy->use_cnt) <= 0 || !psy->desc->set_property)
		return -ENODEV;

	return psy->desc->set_property(psy, psp, val);
}
EXPORT_SYMBOL_GPL(zte_power_supply_set_property);

int zte_power_supply_property_is_writeable(struct zte_power_supply *psy,
					enum zte_power_supply_property psp)
{
	if (atomic_read(&psy->use_cnt) <= 0 ||
			!psy->desc->property_is_writeable)
		return -ENODEV;

	return psy->desc->property_is_writeable(psy, psp);
}
EXPORT_SYMBOL_GPL(zte_power_supply_property_is_writeable);

void zte_power_supply_external_power_changed(struct zte_power_supply *psy)
{
	if (atomic_read(&psy->use_cnt) <= 0 ||
			!psy->desc->external_power_changed)
		return;

	psy->desc->external_power_changed(psy);
}
EXPORT_SYMBOL_GPL(zte_power_supply_external_power_changed);

int zte_power_supply_powers(struct zte_power_supply *psy, struct device *dev)
{
	return sysfs_create_link(&psy->dev.kobj, &dev->kobj, "powers");
}
EXPORT_SYMBOL_GPL(zte_power_supply_powers);

static void zte_power_supply_dev_release(struct device *dev)
{
	struct zte_power_supply *psy = zte_to_power_supply(dev);
	dev_info(dev, "%s\n", __func__);
	kfree(psy);
}

static struct zte_power_supply *__must_check
__zte_power_supply_register(struct device *parent,
				   const struct zte_power_supply_desc *desc,
				   const struct power_supply_config *cfg,
				   bool ws)
{
	struct device *dev;
	struct zte_power_supply *psy;
	int i, rc;

	if (!parent)
		pr_warn("%s: Expected proper parent device for '%s'\n",
			__func__, desc->name);

	if (!desc || !desc->name || !desc->properties || !desc->num_properties)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < desc->num_properties; ++i) {
		if ((desc->properties[i] == POWER_SUPPLY_PROP_USB_TYPE) &&
		    (!desc->usb_types || !desc->num_usb_types))
			return ERR_PTR(-EINVAL);
	}

	psy = kzalloc(sizeof(*psy), GFP_KERNEL);
	if (!psy)
		return ERR_PTR(-ENOMEM);

	dev = &psy->dev;

	device_initialize(dev);

	dev->class = zte_power_supply_class;
	dev->type = &zte_power_supply_dev_type;
	dev->parent = parent;
	dev->release = zte_power_supply_dev_release;
	dev_set_drvdata(dev, psy);
	psy->desc = desc;
	if (cfg) {
		dev->groups = cfg->attr_grp;
		psy->drv_data = cfg->drv_data;
		psy->of_node =
			cfg->fwnode ? to_of_node(cfg->fwnode) : cfg->of_node;
		psy->supplied_to = cfg->supplied_to;
		psy->num_supplicants = cfg->num_supplicants;
	}

	rc = dev_set_name(dev, "%s", desc->name);
	if (rc)
		goto dev_set_name_failed;

	INIT_WORK(&psy->changed_work, zte_power_supply_changed_work);
	INIT_DELAYED_WORK(&psy->deferred_register_work,
			  zte_power_supply_deferred_register_work);

	rc = zte_power_supply_check_supplies(psy);
	if (rc) {
		dev_info(dev, "Not all required supplies found, defer probe\n");
		goto check_supplies_failed;
	}

	spin_lock_init(&psy->changed_lock);
	rc = device_add(dev);
	if (rc)
		goto device_add_failed;

	rc = device_init_wakeup(dev, ws);
	if (rc)
		goto wakeup_init_failed;

	/*
	 * Update use_cnt after any uevents (most notably from device_add()).
	 * We are here still during driver's probe but
	 * the power_supply_uevent() calls back driver's get_property
	 * method so:
	 * 1. Driver did not assigned the returned struct zte_power_supply,
	 * 2. Driver could not finish initialization (anything in its probe
	 *    after calling power_supply_register()).
	 */
	atomic_inc(&psy->use_cnt);
	psy->initialized = true;

	queue_delayed_work(system_power_efficient_wq,
			   &psy->deferred_register_work,
			   POWER_SUPPLY_DEFERRED_REGISTER_TIME);

	return psy;

wakeup_init_failed:
device_add_failed:
check_supplies_failed:
dev_set_name_failed:
	put_device(dev);
	return ERR_PTR(rc);
}

/**
 * power_supply_register() - Register new power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated zte_power_supply on success
 * or ERR_PTR otherwise.
 * Use power_supply_unregister() on returned zte_power_supply pointer to release
 * resources.
 */
struct zte_power_supply *__must_check zte_power_supply_register(struct device *parent,
		const struct zte_power_supply_desc *desc,
		const struct power_supply_config *cfg)
{
	return __zte_power_supply_register(parent, desc, cfg, true);
}
EXPORT_SYMBOL_GPL(zte_power_supply_register);

/**
 * power_supply_register_no_ws() - Register new non-waking-source power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated zte_power_supply on success
 * or ERR_PTR otherwise.
 * Use power_supply_unregister() on returned zte_power_supply pointer to release
 * resources.
 */
struct zte_power_supply *__must_check
zte_power_supply_register_no_ws(struct device *parent,
		const struct zte_power_supply_desc *desc,
		const struct power_supply_config *cfg)
{
	return __zte_power_supply_register(parent, desc, cfg, false);
}
EXPORT_SYMBOL_GPL(zte_power_supply_register_no_ws);

static void zte_devm_power_supply_release(struct device *dev, void *res)
{
	struct zte_power_supply **psy = res;

	zte_power_supply_unregister(*psy);
}

/**
 * devm_power_supply_register() - Register managed power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated zte_power_supply on success
 * or ERR_PTR otherwise.
 * The returned zte_power_supply pointer will be automatically unregistered
 * on driver detach.
 */
struct zte_power_supply *__must_check
zte_devm_power_supply_register(struct device *parent,
		const struct zte_power_supply_desc *desc,
		const struct power_supply_config *cfg)
{
	struct zte_power_supply **ptr, *psy;

	ptr = devres_alloc(zte_devm_power_supply_release, sizeof(*ptr), GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	psy = __zte_power_supply_register(parent, desc, cfg, true);
	if (IS_ERR(psy)) {
		devres_free(ptr);
	} else {
		*ptr = psy;
		devres_add(parent, ptr);
	}
	return psy;
}
EXPORT_SYMBOL_GPL(zte_devm_power_supply_register);

/**
 * devm_power_supply_register_no_ws() - Register managed non-waking-source power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated zte_power_supply on success
 * or ERR_PTR otherwise.
 * The returned zte_power_supply pointer will be automatically unregistered
 * on driver detach.
 */
struct zte_power_supply *__must_check
zte_devm_power_supply_register_no_ws(struct device *parent,
		const struct zte_power_supply_desc *desc,
		const struct power_supply_config *cfg)
{
	struct zte_power_supply **ptr, *psy;

	ptr = devres_alloc(zte_devm_power_supply_release, sizeof(*ptr), GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	psy = __zte_power_supply_register(parent, desc, cfg, false);
	if (IS_ERR(psy)) {
		devres_free(ptr);
	} else {
		*ptr = psy;
		devres_add(parent, ptr);
	}
	return psy;
}
EXPORT_SYMBOL_GPL(zte_devm_power_supply_register_no_ws);

/**
 * power_supply_unregister() - Remove this power supply from system
 * @psy:	Pointer to power supply to unregister
 *
 * Remove this power supply from the system. The resources of power supply
 * will be freed here or on last power_supply_put() call.
 */
void zte_power_supply_unregister(struct zte_power_supply *psy)
{
	WARN_ON(atomic_dec_return(&psy->use_cnt));
	psy->removing = true;
	cancel_work_sync(&psy->changed_work);
	cancel_delayed_work_sync(&psy->deferred_register_work);
	sysfs_remove_link(&psy->dev.kobj, "powers");
	device_init_wakeup(&psy->dev, false);
	device_unregister(&psy->dev);
}
EXPORT_SYMBOL_GPL(zte_power_supply_unregister);

void *zte_power_supply_get_drvdata(struct zte_power_supply *psy)
{
	return psy->drv_data;
}
EXPORT_SYMBOL_GPL(zte_power_supply_get_drvdata);

static int __init zte_power_supply_class_init(void)
{
	zte_power_supply_class = class_create(THIS_MODULE, "zte_power_supply");
	if (IS_ERR(zte_power_supply_class))
		return PTR_ERR(zte_power_supply_class);
	zte_power_supply_class->dev_uevent = zte_power_supply_uevent;

	zte_power_supply_init_attrs(&zte_power_supply_dev_type);
	return 0;
}

static void __exit zte_power_supply_class_exit(void)
{
	class_destroy(zte_power_supply_class);
}

subsys_initcall(zte_power_supply_class_init);
module_exit(zte_power_supply_class_exit);

MODULE_DESCRIPTION("ZTE Universal power supply monitor class");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>, "
	      "Szabolcs Gyurko, "
	      "Anton Vorontsov <cbou@mail.ru>");
MODULE_LICENSE("GPL");

