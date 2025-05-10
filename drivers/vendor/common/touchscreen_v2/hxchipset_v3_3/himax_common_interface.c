/************************************************************************
*
* File Name: himax_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/
#include "himax_common.h"
#include "himax_ic_core.h"
/* #include "himax_inspection.h" */
#ifdef HX_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
#endif

extern int himax_chip_common_resume(struct himax_ts_data *ts);
extern int himax_chip_common_suspend(struct himax_ts_data *ts);
extern uint8_t HX_SMWP_EN;
extern bool fw_update_complete;
#if defined(HX_BOOT_UPGRADE) || defined(HX_ZERO_FLASH)
/* extern char *i_himax_firmware_name; */
#endif
#ifdef HX_USB_DETECT_GLOBAL
extern bool USB_detect_flag;
#endif

int himax_vendor_id = 0;
int himax_test_faied_buffer_length = 0;
int himax_test_failed_count = 0;
int himax_tptest_result = 0;
char himax_firmware_name[50] = {0};
char himax_mp_firmware_name[50] = {0};
char hx_criteria_csv_name[30] = {0};
/* char g_hx_save_file_path[20] = {0};
char g_hx_save_file_name[50] = {0}; */

char *himax_test_failed_node_buffer = NULL;
char *himax_test_temp_buffer = NULL;
u8 *himax_test_failed_node = NULL;
bool	fw_updating = false;
bool get_tpinfo_from_boot_read = false;

#define TEST_RESULT_LENGTH (8 * 1200)
#define TEST_TEMP_LENGTH 8
#ifdef HX_PINCTRL_EN
#define HIMAX_PINCTRL_INIT_STATE "pmx_ts_init"
#endif
#define TP_TEST_INIT		1
#define TP_TEST_START	2
#define TP_TEST_END		3

#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TEST_GT_OPEN				0x0200
#define TEST_GT_SHORT				0x0400

struct tpvendor_t himax_vendor_l[] = {
	{HX_VENDOR_ID_0, HXTS_VENDOR_0_NAME},
	{HX_VENDOR_ID_1, HXTS_VENDOR_1_NAME},
	{HX_VENDOR_ID_2, HXTS_VENDOR_2_NAME},
	{HX_VENDOR_ID_3, HXTS_VENDOR_3_NAME},
	{VENDOR_END, "Unknown"},
};

#ifdef HX_PINCTRL_EN
int himax_platform_pinctrl_init(struct himax_i2c_platform_data *pdata)
{
	int ret = 0;

	/* Get pinctrl if target uses pinctrl */
#ifdef CONFIG_TOUCHSCREEN_HIMAX_SPI
	pdata->ts_pinctrl = devm_pinctrl_get(&(hx_s_ts->spi->dev));
#else
	pdata->ts_pinctrl = devm_pinctrl_get(&(hx_s_ts->client->dev));
#endif
	if (IS_ERR_OR_NULL(pdata->ts_pinctrl)) {
		ret = PTR_ERR(pdata->ts_pinctrl);
		E("Target does not use pinctrl %d\n", ret);
		goto err_pinctrl_get;
	}

	pdata->pinctrl_state_init
	    = pinctrl_lookup_state(pdata->ts_pinctrl, HIMAX_PINCTRL_INIT_STATE);
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_init)) {
		ret = PTR_ERR(pdata->pinctrl_state_init);
		E("Can not lookup %s pinstate %d\n", HIMAX_PINCTRL_INIT_STATE, ret);
		goto err_pinctrl_lookup;
	}

	ret = pinctrl_select_state(pdata->ts_pinctrl, pdata->pinctrl_state_init);
	if (ret < 0) {
		E("failed to select pin to init state");
		goto err_select_init_state;
	}

	return 0;

err_select_init_state:
err_pinctrl_lookup:
	devm_pinctrl_put(pdata->ts_pinctrl);
err_pinctrl_get:
	pdata->ts_pinctrl = NULL;
	return ret;
}
#endif

int himax_get_fw_by_lcdinfo(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(himax_vendor_l); i++) {
		if (strnstr(lcd_name, himax_vendor_l[i].vendor_name, strlen(lcd_name))) {
			himax_vendor_id = himax_vendor_l[i].vendor_id;
			snprintf(himax_firmware_name, sizeof(himax_firmware_name),
				"Himax_firmware_%s.bin", himax_vendor_l[i].vendor_name);
			/* i_himax_firmware_name = himax_firmware_name; */
			snprintf(hx_criteria_csv_name, sizeof(hx_criteria_csv_name),
				"hx_criteria_%s.csv", himax_vendor_l[i].vendor_name);
			I("HXTP firmware name :%s\n", himax_firmware_name);
			snprintf(himax_mp_firmware_name, sizeof(himax_mp_firmware_name),
				"Himax_mp_firmware_%s.bin", himax_vendor_l[i].vendor_name);
			return 0;
		}
	}
	return -EIO;
}

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	struct himax_ts_data *ts = hx_s_ts;

	I("%s enter", __func__);

	if (ts->suspended) {
		I("%s:In suspended", __func__);
		return -EIO;
	}
	switch (himax_vendor_id) {
	case HX_VENDOR_ID_0:
		strlcpy(cdev->ic_tpinfo.vendor_name, HXTS_VENDOR_0_NAME, sizeof(cdev->ic_tpinfo.vendor_name));
		break;
	case HX_VENDOR_ID_1:
		strlcpy(cdev->ic_tpinfo.vendor_name, HXTS_VENDOR_1_NAME, sizeof(cdev->ic_tpinfo.vendor_name));
		break;
	case HX_VENDOR_ID_2:
		strlcpy(cdev->ic_tpinfo.vendor_name, HXTS_VENDOR_2_NAME, sizeof(cdev->ic_tpinfo.vendor_name));
		break;
	case HX_VENDOR_ID_3:
		strlcpy(cdev->ic_tpinfo.vendor_name, HXTS_VENDOR_3_NAME, sizeof(cdev->ic_tpinfo.vendor_name));
		break;
	default:
		strlcpy(cdev->ic_tpinfo.vendor_name, "Unknown.", sizeof(cdev->ic_tpinfo.vendor_name));
		break;
	}
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Himax_%s", ts->chip_name);
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_HIMAX;
	cdev->ic_tpinfo.firmware_ver = hx_s_ic_data->vendor_fw_ver;
	if (hx_s_ts->chip_cell_type == CHIP_IS_ON_CELL) {
		cdev->ic_tpinfo.config_ver = hx_s_ic_data->vendor_config_ver;
	} else {
		cdev->ic_tpinfo.config_ver = hx_s_ic_data->vendor_touch_cfg_ver;
		cdev->ic_tpinfo.display_ver = hx_s_ic_data->vendor_display_cfg_ver;
	}
	cdev->ic_tpinfo.module_id = himax_vendor_id;

	return 0;
}

#ifdef HX_SMART_WAKEUP
static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct himax_ts_data *ts = hx_s_ts;

	I("%s wakeup_gesture_enable val is:%d.\n", __func__, ts->SMWP_enable);
	cdev->b_gesture_enable = ts->SMWP_enable;
	return cdev->b_gesture_enable;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct himax_ts_data *ts = hx_s_ts;

	ts->SMWP_enable = enable;
	ts->gesture_cust_en[0] = ts->SMWP_enable;
	if (!ts->suspended) {
		hx_s_core_fp._set_SMWP_enable(ts->SMWP_enable, ts->suspended);
		HX_SMWP_EN = ts->SMWP_enable;
	} else {
		cdev->tp_suspend_write_gesture = true;
	}
	I("%s: SMART_WAKEUP_enable = %d.\n", __func__, HX_SMWP_EN);
	return ts->SMWP_enable;
}
#endif

static bool himax_suspend_need_awake(struct ztp_device *cdev)
{
#ifdef HX_SMART_WAKEUP
	struct himax_ts_data *ts = hx_s_ts;

	if (!cdev->tp_suspend_write_gesture &&
		(fw_updating || ts->SMWP_enable)) {
		I("tp suspend need awake.\n");
		return true;
	}
#else
	if (fw_updating) {
		I("tp suspend need awake.\n");
		return true;
	}
#endif
	else {
		cdev->tp_suspend_write_gesture = false;
		I("tp suspend dont need awake.\n");
		return false;
	}
}


#ifdef HX_HIGH_SENSE
static int tpd_hsen_read(struct ztp_device *cdev)
{
	struct himax_ts_data *ts = hx_s_ts;

	cdev->b_smart_cover_enable = ts->HSEN_enable;
	cdev->b_glove_enable = ts->HSEN_enable;

	return ts->HSEN_enable;
}

static int tpd_hsen_write(struct ztp_device *cdev, int enable)
{
	struct himax_ts_data *ts = hx_s_ts;

	ts->HSEN_enable = enable;
	if (!ts->suspended)
		g_core_fp.fp_set_HSEN_enable(ts->HSEN_enable, ts->suspended);
	I("%s: HSEN_enable = %d.\n", __func__, ts->HSEN_enable);

	return ts->HSEN_enable;
}
#endif

#ifdef HX_SENSIBILITY
static int himax_set_sensibility_level(struct ztp_device *cdev, u8 level)
{
	struct himax_ts_data *ts = hx_s_ts;

	ts->sensibility_level = level;
	cdev->sensibility_level = ts->sensibility_level;
	I("%s:sensibility = %d.\n", __func__, ts->sensibility_level);
	if (!ts->suspended) {
		if (g_core_fp.fp_set_sensibility_level(ts->sensibility_level) == true) {
			I("%s: sensibility_level write success/n", __func__);
		} else {
			E("%s: sensibility_level write fail/n", __func__);
		}
	}
	return ts->sensibility_level;
}
#endif

/* static int himax_i2c_reg_read(struct ztp_device *cdev, u32 addr, u8 *data, int len)
{
	u8  address[4] = {0};

	address[0] = (u8)addr;
	address[1] = (u8)(addr >> 8);
	address[2] = (u8)(addr >> 16);
	address[3] = (u8)(addr >> 24);
	g_core_fp.fp_register_read(address, len, data, false);
	return 0;
}

static int himax_i2c_reg_write(struct ztp_device *cdev, u32 addr, u8 *data, int len)
{
	u8  address[4] = {0};

	address[0] = (u8)addr;
	address[1] = (u8)(addr >> 8);
	address[2] = (u8)(addr >> 16);
	address[3] = (u8)(addr >> 24);
	g_core_fp.fp_register_write(address, len, data, false);
	return 0;
} */

/* const struct ts_firmware *himax_tp_requeset_firmware(char *file_name)
{
	struct file *file = NULL;
	char file_path[128] = { 0 };
	struct ts_firmware *firmware = NULL;
	int ret = 0;
	loff_t pos = 0;
	loff_t file_len = 0;

	snprintf(file_path, sizeof(file_path), "%s%s", "/sdcard/", file_name);
	file = filp_open(file_path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		E("open %s file fail, try open /vendor/firmware/", file_path);
		snprintf(file_path, sizeof(file_path), "%s%s", "/vendor/firmware/", file_name);
		file = filp_open(file_path, O_RDONLY, 0);
		if (IS_ERR(file)) {
			E("open %s file fail", file_path);
			return NULL;
		}
	}

	firmware = kzalloc(sizeof(struct ts_firmware), GFP_KERNEL);
	if (firmware == NULL) {
		E("Request from file alloc struct firmware failed");
		goto err_close_file;
	}
	file_len = file_inode(file)->i_size;
	firmware->size = (int)file_len;
	I("open %s file ,firmware->size:%d", file_path, firmware->size);
	firmware->data = vmalloc(firmware->size);
	if (firmware->data == NULL) {
		E("alloc firmware data failed");
		goto err_free_firmware;
	}

	pos = 0;
#if (KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE)
	ret = kernel_read(file, firmware->data, file_len, &pos);
#else
	ret = kernel_read(file, 0, firmware->data, file_len);
#endif
	if (ret < 0) {
		E("Request from fs read whole file failed %d", ret);
		goto err_free_firmware_data;
	}
	filp_close(file, NULL);
	return firmware;
err_free_firmware_data:
	vfree(firmware->data);
err_free_firmware:
	kfree(firmware);
err_close_file:
	filp_close(file, NULL);

	return NULL;
} */

static int himax_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	/* char fileName[128] = {0};
#ifdef HX_ZERO_FLASH
	int result = 0;
#else
	int fw_type = 0;
	const struct ts_firmware *firmware = NULL;
#endif

	memset(fileName, 0, sizeof(fileName));
	snprintf(fileName, sizeof(fileName), "%s", fw_name);
	fileName[fwname_len - 1] = '\0';
	I("%s: upgrade from file(%s) start!\n", __func__, fileName);
	himax_int_enable(0);
#ifdef HX_ZERO_FLASH
	I("NOW Running Zero flash update!\n");
	I("%s: upgrade from file(%s) start!\n", __func__, fileName);
	result = g_core_fp.fp_0f_op_file_dirly(fileName);
	if (result) {
		fw_update_complete = false;
		I("Zero flash update fail!\n");
		goto error_fw_upgrade;
	} else {
		fw_update_complete = true;
		I("Zero flash update complete!\n");
	}
	goto firmware_upgrade_done;
#else
	I("NOW Running common flow update!\n");
	I("%s: upgrade from file(%s) start!\n", __func__, fileName);
	firmware = himax_tp_requeset_firmware(fileName);
	if (firmware == NULL) {
		E("Request from file '%s' failed", fileName);
		goto error_fw_upgrade;
	}
	I("%s: FW image: %02X, %02X, %02X, %02X\n", __func__,
			firmware->data[0], firmware->data[1], firmware->data[2], firmware->data[3]);
	fw_type = (firmware->size) / 1024;
	I("Now FW size is : %dk\n", fw_type);
	fw_updating = true;
	switch (fw_type) {
	case 32:
		if (g_core_fp.fp_fts_ctpm_fw_upgrade_with_sys_fs_32k((unsigned char *)firmware->data,
			firmware->size, false) == 0) {
			E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
			fw_update_complete = false;
		} else {
			I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
			fw_update_complete = true;
		}
		break;

	case 60:
		if (g_core_fp.fp_fts_ctpm_fw_upgrade_with_sys_fs_60k((unsigned char *)firmware->data,
			firmware->size, false) == 0) {
			E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
			fw_update_complete = false;
		} else {
			I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
			fw_update_complete = true;
		}
		break;

	case 64:
		if (g_core_fp.fp_fts_ctpm_fw_upgrade_with_sys_fs_64k((unsigned char *)firmware->data,
			firmware->size, false) == 0) {
			E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
			fw_update_complete = false;
		} else {
			I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
			fw_update_complete = true;
		}
		break;

	case 124:
		if (g_core_fp.fp_fts_ctpm_fw_upgrade_with_sys_fs_124k((unsigned char *)firmware->data,
			firmware->size, false) == 0) {
			E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
			fw_update_complete = false;
		} else {
			I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
			fw_update_complete = true;
		}
		break;

	case 128:
		if (g_core_fp.fp_fts_ctpm_fw_upgrade_with_sys_fs_128k((unsigned char *)firmware->data,
			firmware->size, false) == 0) {
			E("%s: TP upgrade error, line: %d\n", __func__, __LINE__);
			fw_update_complete = false;
		} else {
			I("%s: TP upgrade OK, line: %d\n", __func__, __LINE__);
			fw_update_complete = true;
		}
		break;

	default:
		E("%s: Flash command fail: %d\n", __func__, __LINE__);
		fw_update_complete = false;
		break;
	}
	vfree(firmware->data);
	kfree(firmware);
	fw_updating = false;
	goto firmware_upgrade_done;
#endif
firmware_upgrade_done:
	g_core_fp.fp_reload_disable(0);
	g_core_fp.fp_read_FW_ver();
	g_core_fp.fp_touch_information();
#if defined(HX_RST_PIN_FUNC)
	g_core_fp.fp_ic_reset(true, false);
#if defined(HX_ZERO_FLASH)
	if (g_core_fp.fp_0f_reload_to_active)
		g_core_fp.fp_0f_reload_to_active();
#endif
#else
	if (g_core_fp._fw_sts_clear != NULL)
		g_core_fp._fw_sts_clear();
	g_core_fp.fp_sense_on(0x00);
#endif

	himax_int_enable(1);
	return 0;
error_fw_upgrade:
	himax_int_enable(1);
	return -EIO; */
	return 0;
}

/* static int himax_gpio_shutdown_config(void)
{
#ifdef HX_RST_PIN_FUNC
	if (gpio_is_valid(hx_s_ts->rst_gpio)) {
		I("%s\n", __func__);
		gpio_set_value(hx_s_ts->rst_gpio, 0);
	}
#endif
	return 0;
} */

#ifdef HX_LCD_OPERATE_TP_RESET
static void himax_tp_reset_gpio_output(bool value)
{
	I("himax tp reset gpio set value: %d", value);
	gpio_direction_output(hx_s_ts->rst_gpio, value);
}
#endif

static int himax_ts_resume(void * himax_data)
{
	struct himax_ts_data *ts = (struct himax_ts_data *)himax_data;

	himax_chip_common_resume(ts);
	return 0;
}

static int himax_ts_suspend(void *himax_data)
{
	struct himax_ts_data *ts = (struct himax_ts_data *)himax_data;

	himax_chip_common_suspend(ts);
	return 0;
}

static int himax_tp_suspend_show(struct ztp_device *cdev)
{
	struct himax_ts_data *ts = hx_s_ts;

	cdev->tp_suspend = ts->suspended;
	return cdev->tp_suspend;
}

static int himax_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	/* struct himax_ts_data *ts = hx_s_ts;

	if (enable) {
		change_tp_state(OFF);
	} else {
		if (suspend_node == PROC_SUSPEND_NODE)
			g_core_fp.fp_ic_reset(false, false);
		change_tp_state(ON);
	}
	cdev->tp_suspend = ts->suspended;
	return cdev->tp_suspend; */
	return 0;
}

#ifdef HX_DISPLAY_ROTATION
static int himax_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = 0;
	struct himax_ts_data *ts = hx_s_ts;

	cdev->display_rotation = mrotation;
	if (ts->suspended)
		return 0;
	I("%s: display_rotation = %d.\n", __func__, cdev->display_rotation);
	ret = g_core_fp.fp_mrotation_set(cdev->display_rotation);
	if (ret == true) {
		I("%s: mrotation write success/n", __func__);
	} else {
		I("%s: mrotation write fail/n", __func__);
	}
	return cdev->display_rotation;
}
#endif

#ifdef HX_EDGE_LIMIT
static int himax_set_edge_limit_level(struct ztp_device *cdev, u8 level)
{
	int ret = 0;
	struct himax_ts_data *ts = hx_s_ts;

	ts->edge_limit_level = level;
	I("%s: edge limit level = %d.\n", __func__, level);
	if (ts->suspended)
		return 0;
	ret = g_core_fp.fp_edge_limit_level_set(level);
	if (ret == true) {
		I("%s: edge limit level write success/n", __func__);
	} else {
		I("%s: edge limit level write fail/n", __func__);
		return -EIO;
	}
	return level;
}
#endif

#ifdef HX_HEADSET_MODE
static int himax_headset_state_show(struct ztp_device *cdev)
{
	struct himax_ts_data *ts = hx_s_ts;

	cdev->headset_state = ts->headset_state;
	return cdev->headset_state;
}

static int himax_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct himax_ts_data *ts = hx_s_ts;

	ts->headset_state = enable;
	I("%s: headset_state = %d.\n", __func__, ts->headset_state);
	hx_s_core_fp._set_headset_enable(ts->headset_state);

	return ts->headset_state;
}
#endif

#ifdef HX_USB_DETECT_GLOBAL
static bool himax_get_charger_ststus(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}
	I("charger status:%d", status);
	return status;
}

static void himax_work_charger_detect_work(struct work_struct *work)
{
	USB_detect_flag = himax_get_charger_ststus();
}

static int himax_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(hx_s_ts->charger_wq, &hx_s_ts->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int himax_init_charger_notifier(void)
{
	int ret = 0;

	I("Init Charger notifier");

	hx_s_ts->charger_notifier.notifier_call = himax_charger_notify_call;
	ret = power_supply_reg_notifier(&hx_s_ts->charger_notifier);
	return ret;
}

#endif

/* static int himax_print_data2buffer(char *buff_arry[], unsigned int cols, unsigned int rows,
		 s16 *frame_data_words, int idex, int idx, char *name)
{
	int count = 0;
	unsigned int x = 0;
	unsigned int y = 0;

	count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count,
				"\n%s image[%d]:\n", name, idx);
	for (y = 0; y < rows; y++) {
		count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count, "[%2d]", (y + 1));
		for (x = 0; x < cols; x++) {
			count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count,
						"%5d,", frame_data_words[y * cols + x]);
		}
		count += snprintf(buff_arry[idex] + count, RT_DATA_LEN - count, "\n");
	}
	return count;
}
 */
/* static int himax_data_request(s16 *frame_data_words, enum tp_test_type  test_type)
{
	uint8_t *info_data = NULL;
	int index = 0, ret = 0, i = 0;
	int storage_type = 1;
	int diag_command = 0;
	int total_size = 0;
	unsigned int x = 0;
	unsigned int y = 0;
	unsigned int col = 0;
	unsigned int row = 0;

	row = ic_data->HX_TX_NUM;
	col = ic_data->HX_RX_NUM;

	total_size = (ic_data->HX_TX_NUM * ic_data->HX_RX_NUM) * 2;
	info_data = kzalloc((total_size * sizeof(uint8_t)), GFP_KERNEL);
	if (info_data == NULL) {
		ret = -ENOMEM;
		goto sub_end;
	}

	memset(info_data, 0, total_size * sizeof(uint8_t));
	switch (test_type) {
	case RAWDATA_TEST:
		diag_command = 0x02;
		break;
	case DELTA_TEST:
		diag_command = 0x01;
		break;
	default:
		E("%s:the Para is error!\n", __func__);
		ret = -1;
		goto release_mem;
	}

	g_core_fp.fp_diag_register_set(diag_command, storage_type, false);
	g_core_fp.fp_burst_enable(1);
	if (g_core_fp.fp_get_DSRAM_data(info_data, 0) == false) {
		 E("%s:request data error!\n", __func__);
		ret = -1;
	}
	diag_command = 0x00;
	storage_type = 0;
	g_core_fp.fp_diag_register_set(diag_command, storage_type, false);
	if (ret >= 0) {
		for (i = 0, index = 0; index < total_size/2; i += 2, index++) {
			frame_data_words[index] = ((info_data[i + 1] << 8) | info_data[i]);
		}
	}
	for (y = 0; y < row; y++) {
		pr_cont("HXTP[%2d]", (y + 1));
		for (x = 0; x < col; x++) {
			pr_cont("%5d,", frame_data_words[y * col + x]);
		}
		pr_cont("\n");
	}
release_mem:
	if (info_data != NULL)
		kfree(info_data);
sub_end:
	return ret;
	return 0;
} */

/* static int  himax_testing_delta_raw_report(char *buff_arry[], unsigned int num_of_reports)
{

	s16 *frame_data_words = NULL;
	unsigned int col = 0;
	unsigned int row = 0;
	unsigned int idx = 0, idex = 0;
	int retval = 0;

	row = ic_data->HX_TX_NUM;
	col = ic_data->HX_RX_NUM;
	himax_int_enable(0);
	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		E("Failed to allocate frame_data_words mem\n");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		retval = himax_data_request(frame_data_words, RAWDATA_TEST);
		if (retval < 0) {
			E("data_request failed!\n");
			goto DATA_REQUEST_FAILED;
		}

		idex = idx << 1;
		retval = himax_print_data2buffer(buff_arry, col, row,  frame_data_words, idex, idx, "Rawdata");
		if (retval <= 0) {
			E("print_data2buffer rawdata failed!\n");
			goto DATA_REQUEST_FAILED;
		}
		retval = himax_data_request(frame_data_words, DELTA_TEST);
		if (retval < 0) {
			E("data_request failed!\n");
			goto DATA_REQUEST_FAILED;
		}
		idex += 1;
		retval = himax_print_data2buffer(buff_arry, col, row,  frame_data_words, idex, idx, "Delta");
		if (retval <= 0) {
			E("print_data2buffer Delta failed!\n");
			goto DATA_REQUEST_FAILED;
		}
	}

	retval = 0;
	msleep(20);
	I("get tp delta raw data end!\n");
DATA_REQUEST_FAILED:
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	himax_int_enable(1);
	return retval;
	return 0;
} */

static int himax_tpd_get_noise(struct ztp_device *cdev)
{
	/* int retval;
	int i = 0;
	char *buf_arry[RT_DATA_NUM];
	struct tp_runtime_data *tp_rt;
	struct himax_ts_data *ts = hx_s_ts;

	if (ts->suspended)
		return -EIO;

	list_for_each_entry(tp_rt, head, list) {
		buf_arry[i++] = tp_rt->rt_data;
		tp_rt->is_empty = false;
	}
	get_tpinfo_from_boot_read = true;
	retval = himax_testing_delta_raw_report(buf_arry, RT_DATA_NUM >> 2);
	if (retval < 0) {
		E("%s: get_raw_noise failed!\n",  __func__);
		return retval;
	} */
	return 0;
}

/* himax TP slef test*/


static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	I("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", himax_tptest_result, hx_s_ic_data->tx_num,
		hx_s_ic_data->rx_num, himax_test_failed_count);
	I("tpd test result:%d .\n", himax_tptest_result);

	E("tpd test:%s.\n", buf);
	num_read_chars = i_len;
	return num_read_chars;
}

void himax_tpd_test_result_check(uint32_t test_result)
{
	/* int i = 0;

	if (test_result == 0) {
		himax_tptest_result = 0;
		return;
	}

	for (i = 0; i < HX_CRITERIA_ITEM - 1; i++) {
		if (g_test_item_flag[i] == 1) {
			I("%s : %s\n", g_himax_inspection_mode[i],
				((test_result & (1 << (i + ERR_SFT)))
				 == (1 << (i + ERR_SFT))) ? "Fail":"OK");
			if ((test_result & (1 << (i + ERR_SFT))) == (1 << (i + ERR_SFT))) {
				if (strnstr(g_himax_inspection_mode[i], "OPEN",
					strlen(g_himax_inspection_mode[i]))) {
					himax_tptest_result = himax_tptest_result | TEST_GT_OPEN;
				} else if (strnstr(g_himax_inspection_mode[i], "SHORT",
					strlen(g_himax_inspection_mode[i]))) {
					himax_tptest_result = himax_tptest_result | TEST_GT_SHORT;
				} else {
					himax_tptest_result = himax_tptest_result | TEST_BEYOND_MAX_LIMIT
										| TEST_BEYOND_MIN_LIMIT;
				}
			}
		}
	} */
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{

	I("%s: enter, %d\n", __func__, __LINE__);

	if (hx_s_ts->suspended == 1) {
		E("%s: please do self test in normal active mode\n", __func__);
		return HX_INIT_FAIL;
	}

	if (hx_s_ts->in_self_test == 1) {
		W("%s: Self test is running now!\n", __func__);
		return 0;
	}
	hx_s_ts->in_self_test = 1;

	himax_int_enable(0);

	himax_tptest_result = hx_s_core_fp._chip_self_test(NULL, NULL);

#if defined(HX_EXCP_RECOVERY)
	HX_EXCP_RESET_ACTIVATE = 1;
#endif
	himax_int_enable(1);

	hx_s_ts->in_self_test = 0;
	return 0;
}

/* static int tpd_test_channel_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;

	num_read_chars =
	    snprintf(buf, PAGE_SIZE, "%d %d", ic_data->HX_TX_NUM, ic_data->HX_RX_NUM);

	return num_read_chars;
} */

void himax_tpd_register_fw_class(void)
{
	I("tpd_register_fw_class\n");
	himax_get_fw_by_lcdinfo();
#ifdef HX_USB_DETECT_GLOBAL
	hx_s_ts->charger_wq = create_singlethread_workqueue("HMX_charger_detect");
	if (!hx_s_ts->charger_wq) {
		E(" allocate charger_wq failed\n");
	} else  {
		USB_detect_flag = himax_get_charger_ststus();
		INIT_DELAYED_WORK(&hx_s_ts->charger_work, himax_work_charger_detect_work);
		himax_init_charger_notifier();
	}
#endif
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;
#ifdef HX_SMART_WAKEUP
	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;
#endif
#ifdef HX_HIGH_SENSE
	tpd_cdev->get_smart_cover = tpd_hsen_read;
	tpd_cdev->set_smart_cover = tpd_hsen_write;
	tpd_cdev->get_glove_mode = tpd_hsen_read;
	tpd_cdev->set_glove_mode = tpd_hsen_write;
#endif
/* 	tpd_cdev->tp_i2c_16bor32b_reg_read = himax_i2c_reg_read;
	tpd_cdev->tp_i2c_16bor32b_reg_write = himax_i2c_reg_write;
	tpd_cdev->reg_char_num = REG_CHAR_NUM_8; */
	tpd_cdev->tp_fw_upgrade = himax_tp_fw_upgrade;
	/* tpd_cdev->tpd_gpio_shutdown = himax_gpio_shutdown_config; */
	tpd_cdev->tpd_suspend_need_awake = himax_suspend_need_awake;
	tpd_cdev->tp_suspend_show = himax_tp_suspend_show;
	tpd_cdev->set_tp_suspend = himax_set_tp_suspend;
#ifdef HX_SENSIBILITY
	tpd_cdev->set_sensibility = himax_set_sensibility_level;
	hx_s_ts->sensibility_level = 1;
#endif
#ifdef HX_LCD_OPERATE_TP_RESET
	tpd_cdev->tp_reset_gpio_output = himax_tp_reset_gpio_output;
#endif
#ifdef HX_DISPLAY_ROTATION
	tpd_cdev->set_display_rotation = himax_set_display_rotation;
#endif
#ifdef HX_HEADSET_MODE
	tpd_cdev->headset_state_show = himax_headset_state_show;
	tpd_cdev->set_headset_state = himax_set_headset_state;
#endif
#ifdef HX_EDGE_LIMIT
	/* tpd_cdev->set_edge_limit_level = himax_set_edge_limit_level;
	hx_s_ts->edge_limit_level = 0; */
#endif

	tpd_cdev->max_x = hx_s_ts->pdata->abs_x_max;
	tpd_cdev->max_y = hx_s_ts->pdata->abs_y_max;

	tpd_cdev->get_noise = himax_tpd_get_noise;
	tpd_cdev->tp_data = hx_s_ts;
	tpd_cdev->tp_resume_func = himax_ts_resume;
	tpd_cdev->tp_suspend_func = himax_ts_suspend;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
/*	tpd_cdev->tpd_test_get_channel_info = tpd_test_channel_show; */
	tpd_cdev->tp_resume_before_lcd_cmd = true;
/* 	ufp_tp_ops.tp_data = hx_s_ts;
	ufp_tp_ops.tp_resume_func = himax_ts_resume;
	ufp_tp_ops.tp_suspend_func = himax_ts_suspend; */
}
