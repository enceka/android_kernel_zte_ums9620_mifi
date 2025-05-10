/************************************************************************
*
* File Name: omnivision_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

#include "omnivision_common_interface.h"

char ovt_tcm_vendor_name[MAX_NAME_LEN_50] = { 0 };
char ovt_tcm_save_file_path[MAX_NAME_LEN_50] = { 0 };
char ovt_tcm_save_file_name[MAX_NAME_LEN_50] = { 0 };
int ovt_tcm_tptest_result = 0;
struct tpvendor_t ovt_tcm_vendor_info[] = {
	{OVT_TCM_MODULE1_ID, OVT_TCM_MODULE1_LCD_NAME },
	{OVT_TCM_MODULE2_ID, OVT_TCM_MODULE2_LCD_NAME },
	{OVT_TCM_MODULE3_ID, OVT_TCM_MODULE3_LCD_NAME },
	{VENDOR_END, "Unknown"},
};
enum ovt_tcm_sensibility_level {
	MIN_SENSI = 0,
	NORMAL_SENSI = 1,
	HIGER_SENSI = 2,
	HIGEST_SENSI = 3,
	MAX_SENSI = 1000,
};

extern void ovt_tcm_resume_work_func(struct work_struct *work);
extern int ovt_tcm_resume(struct device *dev);
extern int ovt_tcm_suspend(struct device *dev);
extern int zeroflash_parse_fw_image(void);
extern int testing_raw_data(void);
extern int testing_delta_data(void);

extern struct zeroflash_hcd *zeroflash_hcd;

static int rst_gpio = 0;

extern struct testing_hcd *testing_hcd;
#define GET_NOISE_DATA_TIMES 1

#ifdef OVT_TCM_PINCTRL_EN
#define OVT_TCM_PINCTRL_INIT_STATE "pmx_ts_init"
int ovt_tcm_pinctrl_init(struct spi_device *spi, struct ovt_tcm_board_data *bdata)
{
	int ret = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	/* Get pinctrl if target uses pinctrl */
	bdata->ts_pinctrl = devm_pinctrl_get(&spi->dev);
	if (IS_ERR_OR_NULL(bdata->ts_pinctrl)) {
		ret = PTR_ERR(bdata->ts_pinctrl);
		ovt_info(ERR_LOG, "%s:devm_pinctrl_get failed, ret=%d\n", __func__, ret);
		goto err_pinctrl_get;
	} else {
		ovt_info(INFO_LOG, "%s:devm_pinctrl_get success\n", __func__);
	}

	bdata->pinctrl_state_init
	    = pinctrl_lookup_state(bdata->ts_pinctrl, OVT_TCM_PINCTRL_INIT_STATE);
	if (IS_ERR_OR_NULL(bdata->pinctrl_state_init)) {
		ret = PTR_ERR(bdata->pinctrl_state_init);
		ovt_info(ERR_LOG, "%s:pinctrl_lookup_state %s failed, ret=%d\n", __func__, OVT_TCM_PINCTRL_INIT_STATE, ret);
		goto err_pinctrl_lookup;
	} else {
		ovt_info(INFO_LOG, "%s:pinctrl_lookup_state %s success\n", __func__, OVT_TCM_PINCTRL_INIT_STATE);
	}

	ret = pinctrl_select_state(bdata->ts_pinctrl, bdata->pinctrl_state_init);
	if (ret < 0) {
		ovt_info(ERR_LOG, "%s:failed to select pin to init state, ret=%d\n", __func__, ret);
		goto err_select_init_state;
	} else {
		ovt_info(INFO_LOG, "%s:success to select pin to init state\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;

err_select_init_state:
err_pinctrl_lookup:
	devm_pinctrl_put(bdata->ts_pinctrl);
err_pinctrl_get:
	bdata->ts_pinctrl = NULL;
	return ret;
}
#endif

int get_ovt_tcm_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0 ; i < (ARRAY_SIZE(ovt_tcm_vendor_info) - 1) ; i ++) {
		ovt_info(INFO_LOG, "%s:%d--->%s\n", __func__, i, ovt_tcm_vendor_info[i].vendor_name);
		if (strnstr(lcd_name, ovt_tcm_vendor_info[i].vendor_name, strlen(lcd_name))) {
			ovt_info(INFO_LOG, "%s:get_lcd_panel_name find\n", __func__);
			break;
		}
	}

	strlcpy(ovt_tcm_vendor_name, ovt_tcm_vendor_info[i].vendor_name, sizeof(ovt_tcm_vendor_name));

	ovt_info(INFO_LOG, "ovt_tcm_vendor_name:%s\n", ovt_tcm_vendor_name);
	return ovt_tcm_vendor_info[i].vendor_id;
}

static int ovt_tcm_init_tpinfo(struct ztp_device *cdev)
{
	int retval = 0;
	int vendor_id;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
#ifndef USE_SPI_BUS
	struct i2c_client *i2c = to_i2c_client(tcm_hcd->pdev->dev.parent);
#endif
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:error, zeroflash_hcd is NULL!\n", __func__);
		return -EIO;
	}
	if (tcm_hcd->in_suspend)
		return -EIO;

	mutex_lock(&tcm_hcd->extif_mutex);

	vendor_id = get_ovt_tcm_module_info_from_lcd();
	strlcpy(cdev->ic_tpinfo.vendor_name, ovt_tcm_vendor_name, sizeof(cdev->ic_tpinfo.vendor_name));
	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Omnivision");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_OMNIVISION;
	cdev->ic_tpinfo.module_id = vendor_id;
	cdev->ic_tpinfo.firmware_ver = zeroflash_hcd->tcm_hcd->zte_ctrl.fw_ver;
#ifdef USE_SPI_BUS
	cdev->ic_tpinfo.spi_num = SPI_NUM;
#else
	cdev->ic_tpinfo.i2c_addr = i2c->addr;
#endif

	mutex_unlock(&tcm_hcd->extif_mutex);

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_get_headset_state(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->headset_state = tcm_hcd->zte_ctrl.headset_state;

	ovt_info(INFO_LOG, "%s:headset_state=%d\n", __func__, cdev->headset_state);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->headset_state;
}

static int ovt_tcm_set_headset_state(struct ztp_device *cdev, int enable)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:error, zeroflash_hcd is NULL!\n", __func__);
		return -EIO;
	}

	tcm_hcd->zte_ctrl.headset_state = enable;
	ovt_info(INFO_LOG, "%s: headset_state=%d\n", __func__, tcm_hcd->zte_ctrl.headset_state);

	if (tcm_hcd->in_suspend || !zeroflash_hcd->fw_ready) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend or fw not ready!\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->in_suspend) {
		if (tcm_hcd->zte_ctrl.headset_state)
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 1);
		else
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 0);
	}

	ovt_info(INFO_LOG, "%s:retval=%d, headset_state=%d\n", __func__, retval, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return tcm_hcd->zte_ctrl.headset_state;
}

int ovt_tcm_resume_set_headset_status(struct ovt_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	if (tcm_hcd->zte_ctrl.headset_state) {
		retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 1);
		ovt_info(INFO_LOG, "%s:headset_state=1, retval=%d\n", __func__, retval);
	} else {
		ovt_info(INFO_LOG, "%s:headset_state=0\n", __func__);
	}

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_get_sensibility(struct ztp_device *cdev)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->sensibility_enable = tcm_hcd->zte_ctrl.sensibility_level;

	ovt_info(INFO_LOG, "%s:sensibility_level=%d\n", __func__, cdev->sensibility_enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (tcm_hcd->in_suspend) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend!\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	tcm_hcd->zte_ctrl.sensibility_level = enable;

	switch (enable) {
		case NORMAL_SENSI:
			ovt_info(INFO_LOG, "ovt tp is normal sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 0);
			break;
		case HIGER_SENSI:
			ovt_info(INFO_LOG, "ovt tp is higher sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 1);
			break;
		case HIGEST_SENSI:
			ovt_info(INFO_LOG, "ovt tp is highest sensibility\n");
			retval = tcm_hcd->set_dynamic_config(tcm_hcd, SENSIBILITY_CMD, 2);
			break;
		default:
			ovt_info(ERR_LOG, "Unsupport tp sensibility level\n");
			break;
	}

	ovt_info(INFO_LOG, "%s:retval=%d, sensibility_level=%d\n", __func__, retval, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return retval;
}

static int ovt_tcm_get_tp_suspend(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->tp_suspend = tcm_hcd->in_suspend;

	ovt_info(INFO_LOG, "%s:tp_suspend=%d\n", __func__, cdev->tp_suspend);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->tp_suspend;
}

static int ovt_tcm_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);
	return 0;
}

static int ovt_tcm_resume_func(void *unused_tcm_hcd)
{
	ovt_info(INFO_LOG, "%s enter\n", __func__);

	if (zeroflash_hcd) {
		return ovt_tcm_resume(&zeroflash_hcd->tcm_hcd->pdev->dev);
	} else {
		ovt_info(ERR_LOG, "%s not implement\n", __func__);
		return 0;
	}
}

static int ovt_tcm_suspend_func(void *unused_tcm_hcd)
{
	ovt_info(INFO_LOG, "%s enter\n", __func__);

	if (zeroflash_hcd) {
		return ovt_tcm_suspend(&zeroflash_hcd->tcm_hcd->pdev->dev);
	} else {
		ovt_info(ERR_LOG, "%s not implement\n", __func__);
		return 0;
	}
}

static int ovt_tcm_get_wakegesture(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	cdev->b_gesture_enable = tcm_hcd->wakeup_gesture_enabled;

	ovt_info(INFO_LOG, "%s:gesture_enable=%d\n", __func__, cdev->b_gesture_enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;
}

static int ovt_tcm_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	tcm_hcd->enter_gesture = enable;
	if (tcm_hcd->in_suspend) {
		cdev->tp_suspend_write_gesture = true;
		ovt_info(ERR_LOG, "%s:error, ovt tcm in suspend!\n", __func__);
	} else {
		ovt_info(INFO_LOG, "%s:ovt tcm in resume!\n", __func__);
		tcm_hcd->wakeup_gesture_enabled = enable;
	}

	ovt_info(INFO_LOG, "%s:gesture_enable=%d\n", __func__, enable);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;
}

static bool ovt_tcm_suspend_need_awake(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!cdev->tp_suspend_write_gesture && tcm_hcd->wakeup_gesture_enabled) {
		ovt_info(INFO_LOG, "%s:ovt tcm suspend need awake\n", __func__);
		return true;
	}
	cdev->tp_suspend_write_gesture = false;
	ovt_info(INFO_LOG, "%s:ovt tcm suspend dont need awake\n", __func__);
	return false;
}

static int ovt_tcm_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	bool display_rotation_old = tcm_hcd->zte_ctrl.display_rotation;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:error, zeroflash_hcd is NULL!\n", __func__);
		return -EIO;
	}

	cdev->display_rotation = mrotation;
	tcm_hcd->zte_ctrl.display_rotation = cdev->display_rotation;
	if (tcm_hcd->in_suspend || !zeroflash_hcd->fw_ready) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend or fw not ready!\n", __func__);
		return -EIO;
	}

	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	ovt_info(INFO_LOG, "%s:display_rotation=%d\n", __func__, cdev->display_rotation);
	if (tcm_hcd->zte_ctrl.display_rotation == display_rotation_old) {
		ovt_info(INFO_LOG, "%s:tcm_hcd->display_rotation not change", __func__);
		return -EIO;
	}
	switch (cdev->display_rotation) {
		case mRotatin_0:
			ovt_info(INFO_LOG, "mRotatin_0\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 0);
			break;
		case mRotatin_90:/*USB on right*/
			ovt_info(INFO_LOG, "mRotatin_90\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 1);
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, HORIZONTAL_CMD, 3);
			break;
		case mRotatin_180:
			ovt_info(INFO_LOG, "mRotatin_180\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 0);
			break;
		case mRotatin_270:/*USB on left*/
			ovt_info(INFO_LOG, "mRotatin_270\n");
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, VERTICAL_CMD, 1);
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, HORIZONTAL_CMD, 7);
			break;
		default:
			break;
	}
	if (ret) {
		ovt_info(ERR_LOG, "Set display rotation failed!\n");
	} else {
		ovt_info(INFO_LOG, "Set display rotation success!\n");
	}

	ovt_info(INFO_LOG, "%s:ret=%d\n", __func__, ret);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return cdev->display_rotation;
}

static int ovt_tcm_charger_state_notify(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	bool charger_mode_old = tcm_hcd->zte_ctrl.charger_state;
	int ret = 0;

	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:error, zeroflash_hcd is NULL!\n", __func__);
		return -EIO;
	}
	tcm_hcd->zte_ctrl.charger_state = cdev->charger_mode;
	if (tcm_hcd->in_suspend || !zeroflash_hcd->fw_ready) {
		ovt_info(ERR_LOG, "%s:error, ovt tp in suspend or fw not ready!\n", __func__);
		return -EIO;
	}
	ovt_info(INFO_LOG, "%s: charger_state=%d\n", __func__, tcm_hcd->zte_ctrl.charger_state);
	if (tcm_hcd->zte_ctrl.charger_state != charger_mode_old) {
		if (tcm_hcd->zte_ctrl.charger_state) {
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 1);
			ovt_info(INFO_LOG, "%s:enter_charger, ret=%d\n", __func__, ret);
		} else {
			ret = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 0);
			ovt_info(INFO_LOG, "%s:leave_charger, ret=%d\n", __func__, ret);
		}
	}
	return 0;
}

int ovt_tcm_ex_mode_recovery(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;
	int retval = 0;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);
	if (!tcm_hcd->set_dynamic_config) {
		ovt_info(ERR_LOG, "%s:tcm_hcd->set_dynamic_config in null\n", __func__);
		return -EIO;
	}

	if (tcm_hcd->zte_ctrl.charger_state) {
		retval = tcm_hcd->set_dynamic_config(tcm_hcd, DC_CHARGER_CONNECTED, 1);
		ovt_info(INFO_LOG, "%s:enter_charger, retval=%d\n", __func__, retval);
	} else {
		ovt_info(INFO_LOG, "%s:leave_charger\n", __func__);
	}

	if (tcm_hcd->zte_ctrl.headset_state) {
		retval = tcm_hcd->set_dynamic_config(tcm_hcd, HEADSET_CMD, 1);
		ovt_info(INFO_LOG, "%s:headset_state=1, retval=%d\n", __func__, retval);
	} else {
		ovt_info(INFO_LOG, "%s:headset_state=0\n", __func__);
	}
	return 0;
}

static int ovt_tcm_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	int retval = 0;

	if (cdev->tp_firmware == NULL || cdev->tp_firmware->data == NULL) {
		ovt_info(ERR_LOG,"cdev->tp_firmware is NULL");
		return -EIO;
	}
	if (!zeroflash_hcd) {
		ovt_info(ERR_LOG, "%s:error, zeroflash_hcd is NULL!\n", __func__);
		return -EIO;
	}
	if (zeroflash_hcd->adb_fw == NULL) {
		zeroflash_hcd->adb_fw  = kzalloc(sizeof(struct firmware), GFP_KERNEL);
		if (zeroflash_hcd->adb_fw  == NULL) {
			ovt_info(ERR_LOG,"Request firmware alloc failed");
			return -EIO;
		}
	}
	if (zeroflash_hcd->adb_fw->data != NULL) {
		vfree(zeroflash_hcd->adb_fw->data);
	}
	zeroflash_hcd->adb_fw->size = cdev->tp_firmware->size;
	zeroflash_hcd->adb_fw->data = vmalloc(zeroflash_hcd->adb_fw->size);
	if (zeroflash_hcd->adb_fw->data == NULL) {
		ovt_info(ERR_LOG,"alloc firmware data failed");
		return -EIO;
	}
	memcpy((char *)zeroflash_hcd->adb_fw->data, (char *)cdev->tp_firmware->data, cdev->tp_firmware->size);
	zeroflash_hcd->image = zeroflash_hcd->adb_fw->data;

	retval = zeroflash_parse_fw_image();
	if (retval < 0) {
		ovt_info(ERR_LOG,
				"Failed to parse firmware image\n");
		return retval;
	} else {
		ovt_info(INFO_LOG, "%s:Success to parse firmware image\n", __func__);
	}
	return retval;
}

static int ovt_tcm_data_request(unsigned int cols, unsigned int rows, s16 *frame_data_words, enum tp_test_type  test_type)
{
	uint8_t *info_data = NULL;
	short data;
	int ret = 0, i = 0, j = 0, idx = 0;
	unsigned char *buf;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	switch (test_type) {
	case RAWDATA_TEST:
		ret = testing_raw_data();		
		if (ret < 0) {
			ovt_info(ERR_LOG, "Failed to get rawdata\n");
			goto test_end;
		} else {
			ovt_info(INFO_LOG, "Success to get rawdata\n");
		}
		break;
	case DELTA_TEST:
		ret = testing_delta_data();		
		if (ret < 0) {
			ovt_info(ERR_LOG, "Failed to get diffdata\n");
			goto test_end;
		} else {
			ovt_info(INFO_LOG, "Success to get diffdata\n");
		}
		break;
	default:
		ovt_info(ERR_LOG,"%s:the Para is error!", __func__);
		break;
	}

	buf = testing_hcd->report.buf;

	for (i = 0; i < rows; i++) {
		pr_cont("ovt_info CTP[%2d]", (i + 1));
		for (j = 0; j < cols; j++) {
			data = (short)le2_to_uint(&buf[idx * 2]);
			pr_cont("%5d,", data);
			frame_data_words[idx] = data;
			idx++;
		}
		pr_cont("\n");
	}
	if (info_data != NULL)
		kfree(info_data);

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return 0;

test_end:
	return ret;
}

int ovt_tcm_copy_delta_raw_data(struct ztp_device *cdev, s16 *frame_data_words, int *len, enum tp_test_type  test_type)
{
	unsigned int col = 0;
	unsigned int row = 0;
	int retval = 0;
	int i = 0;
	int j = 1;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	row = le2_to_uint(tcm_hcd->app_info.num_of_image_rows);
	col = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);

	retval = ovt_tcm_data_request(col, row, frame_data_words, test_type);
	if (retval < 0) {
		ovt_info(ERR_LOG,"data_request failed!");
		return retval;
	}

	switch (test_type) {
	case RAWDATA_TEST:
		*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len,
			"RawData:\n");
		break;
	case DELTA_TEST:
		*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len,
			"DiffData:\n");
		break;
	default:
		ovt_info(ERR_LOG,"%s:the Para is error!", __func__);
	}

	*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len,
			"CTP[%2d]", j);
	for (i = 0; i < row * col; i++) {
		*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len,
			"%5d,", frame_data_words[i]);
		if((i + 1) % col == 0) {
			*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len, "\n");
			if((i + 1) < row * col) {
				*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len,
					"CTP[%2d]", (++j));
			}
		}
	}
	*len += snprintf((char *)(cdev->tp_firmware->data + *len), RT_DATA_LEN * 10 - *len, "\n\n");
	return retval;
}

static int  ovt_tcm_get_noise_data(struct ztp_device *cdev, struct ovt_tcm_hcd *tcm_hcd, unsigned int num_of_reports)
{
	s16 *frame_data_words = NULL;
	unsigned int col = 0, row = 0;
	unsigned int idx = 0;
	int retval = 0;
	int len = 0;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	row = le2_to_uint(tcm_hcd->app_info.num_of_image_rows);
	col = le2_to_uint(tcm_hcd->app_info.num_of_image_cols);

	ovt_info(INFO_LOG, "%s:RAWDATA_ROW:%d\n", __func__, row);
	ovt_info(INFO_LOG, "%s:RAWDATA_COL:%d\n", __func__, col);

	frame_data_words = kcalloc((row * col), sizeof(s16), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		ovt_info(ERR_LOG, "Failed to allocate frame_data_words mem\n");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}

	for (idx = 0; idx < num_of_reports; idx++) {
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		retval = ovt_tcm_copy_delta_raw_data(cdev, frame_data_words, &len, RAWDATA_TEST);
		if (retval < 0) {
			goto DATA_REQUEST_FAILED;
		}

		retval = ovt_tcm_copy_delta_raw_data(cdev, frame_data_words, &len, DELTA_TEST);
		if (retval < 0) {
			goto DATA_REQUEST_FAILED;
		}
	}
	retval = 0;
	msleep(20);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);

DATA_REQUEST_FAILED:
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
	return retval;
}

static int ovt_tcm_get_noise(struct ztp_device *cdev)
{
	int ret =0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		ovt_info(ERR_LOG, "%s alloc tp firmware data fai\n", __func__);
		return -ENOMEM;
	}
	ret = ovt_tcm_get_noise_data(cdev, tcm_hcd, 5);
	if (ret) {
		ovt_info(ERR_LOG, "%s:get_noise failed\n",  __func__);
		return ret;
	} else {
		ovt_info(INFO_LOG, "%s:get_noise success\n",  __func__);
	}
	return 0;
}

#ifdef OVT_TCM_LCD_OPERATE_TP_RESET
static void ovt_tcm_reset_gpio_output(bool value)
{

	ovt_info(INFO_LOG, "%s:value=%d, rst_gpio=%d\n", __func__, value, rst_gpio);

	if (rst_gpio) {
		gpio_direction_output(rst_gpio, value);
	}
}
#endif

static int ovt_tcm_shutdown(struct ztp_device *cdev)
{
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	ovt_tcm_suspend_func(tcm_hcd);
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	struct ovt_tcm_hcd *tcm_hcd = (struct ovt_tcm_hcd *)cdev->private;

	ovt_info(DEBUG_LOG, "%s enter\n", __func__);	

	ovt_info(INFO_LOG, "%s:RAWDATA_COL:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_cols);
	ovt_info(INFO_LOG, "%s:RAWDATA_ROW:%d\n", __func__, tcm_hcd->zte_ctrl.rawdata_rows);
	num_read_chars = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", ovt_tcm_tptest_result,
		tcm_hcd->zte_ctrl.rawdata_cols, tcm_hcd->zte_ctrl.rawdata_rows, 0);
	ovt_info(INFO_LOG, "%s:ovt tcm test:%s\n", __func__, buf);
	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int result = 0, retry = 0;

	while (retry < 3) {		
		ovt_tcm_tptest_result = 0;
		result = testing_do_testing();	
		if (result) {
			ovt_info(INFO_LOG, "ovt_tcm_test %d times fail", (retry + 1));
			result = 0;
			retry ++;
			if (rst_gpio) {
				gpio_direction_output(rst_gpio, 0);
				usleep_range(5000, 5001);
				gpio_direction_output(rst_gpio, 1);
				ovt_info(INFO_LOG, "msleep 1s for rst in test fail\n");
				msleep(1000);
			}
		} else {
			ovt_info(INFO_LOG, "ovt_tcm_test %d times pass", (retry + 1));
			break;
		}

	}
	return 0;
}

void ominivision_tpd_register_fw_class(struct ovt_tcm_hcd *tcm_hcd)
{
	ovt_info(DEBUG_LOG, "%s enter\n", __func__);

	tpd_cdev->private = (void *)tcm_hcd;
	tpd_cdev->get_tpinfo = ovt_tcm_init_tpinfo;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;

	tpd_cdev->headset_state_show = ovt_tcm_get_headset_state;
	tpd_cdev->set_headset_state = ovt_tcm_set_headset_state;

	tpd_cdev->get_sensibility = ovt_tcm_get_sensibility;
	tpd_cdev->set_sensibility = ovt_tcm_set_sensibility;

	tpd_cdev->tp_suspend_show = ovt_tcm_get_tp_suspend;
	tpd_cdev->set_tp_suspend = ovt_tcm_set_tp_suspend;

	tpd_cdev->tp_data = tcm_hcd;
	tpd_cdev->tp_resume_func = ovt_tcm_resume_func;
	tpd_cdev->tp_suspend_func = ovt_tcm_suspend_func;


	tpd_cdev->get_gesture = ovt_tcm_get_wakegesture;
	tpd_cdev->wake_gesture = ovt_tcm_enable_wakegesture;

	tpd_cdev->tpd_suspend_need_awake = ovt_tcm_suspend_need_awake;

	tpd_cdev->set_display_rotation = ovt_tcm_set_display_rotation;

	tpd_cdev->tp_fw_upgrade = ovt_tcm_fw_upgrade;
	tpd_cdev->get_noise = ovt_tcm_get_noise;
	tpd_cdev->tpd_shutdown = ovt_tcm_shutdown;
	tpd_cdev->tpd_send_cmd = ovt_tcm_ex_mode_recovery;
	tpd_cdev->charger_state_notify = ovt_tcm_charger_state_notify;
	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));
#ifdef OVT_TCM_LCD_OPERATE_TP_RESET
	rst_gpio = tcm_hcd->hw_if->bdata->reset_gpio;
	ovt_info(INFO_LOG, "%s:rst_gpio=%d\n", __func__, rst_gpio);
	tpd_cdev->tp_reset_gpio_output = ovt_tcm_reset_gpio_output;
#endif

	tpd_cdev->max_x = tcm_hcd->zte_ctrl.panel_max_x;
	tpd_cdev->max_y = tcm_hcd->zte_ctrl.panel_max_y;
	tcm_hcd->zte_ctrl.charger_state = false;
	tcm_hcd->zte_ctrl.headset_state = false;
	tcm_hcd->zte_ctrl.display_rotation = 0;

	ovt_info(DEBUG_LOG, "%s exit\n", __func__);
}
