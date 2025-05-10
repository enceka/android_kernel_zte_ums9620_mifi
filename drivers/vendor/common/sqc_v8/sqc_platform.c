#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of.h>
#include "sqc_platform.h"

int sqc_get_boot_mode(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line;
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (ret)
		return ret;

	if (strstr(cmd_line, "androidboot.mode=charger"))
		return SQC_BOOT_CHARGE;
	else
		return SQC_BOOT_NORMAL;


}
EXPORT_SYMBOL_GPL(sqc_get_boot_mode);
