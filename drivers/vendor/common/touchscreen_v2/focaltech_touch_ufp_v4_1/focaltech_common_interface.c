/************************************************************************
*
* File Name: fts_common_interface.c
*
*  *   Version: v1.0
*
************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/

#include "focaltech_core.h"
#include "focaltech_test.h"
#include <linux/kernel.h>
#include <linux/power_supply.h>
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define TEST_RESULT_LENGTH (8 * 1200)
#define TEST_TEMP_LENGTH 8
#define TEST_PASS	0
#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TP_TEST_INIT		1
#define TP_TEST_START	2
#define TP_TEST_END		3

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

char g_fts_ini_filename[MAX_INI_FILE_NAME_LEN] = {0};
char fts_vendor_name[20] = { 0 };
int fts_tptest_result = 0;

extern struct fts_ts_data *fts_data;
extern struct fts_test *fts_ftest;
extern int fts_ts_suspend(struct device *dev);
extern int fts_ts_resume(struct device *dev);
extern int fts_test_init_basicinfo(struct fts_test *tdata);
extern int fts_test_entry(char *ini_file_name);
extern int fts_ex_mode_switch(enum _ex_mode mode, u8 value);


struct tpvendor_t fts_vendor_info[] = {
	{FTS_MODULE_ID, FTS_MODULE_NAME },
	{FTS_MODULE2_ID, FTS_MODULE2_NAME },
	{FTS_MODULE3_ID, FTS_MODULE3_NAME },
	{VENDOR_END, "Unknown"},
};

int get_fts_module_info_from_lcd(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(fts_vendor_info); i++) {
		if (strnstr(lcd_name, fts_vendor_info[i].vendor_name, strlen(lcd_name))) {
			strlcpy(fts_vendor_name, fts_vendor_info[i].vendor_name, sizeof(fts_vendor_name));
			return  fts_vendor_info[i].vendor_id;
		}
	}
	return -EINVAL;
}


static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 fwver_in_chip = 0;
	u8 vendorid_in_chip = 0;
	u8 chipid_in_chip = 0;
	u8 lcdver_in_chip = 0;
	u8 retry = 0;

	if (fts_data->suspended) {
		FTS_ERROR("fts tp in suspned");
		return -EIO;
	}

	while (retry++ < 5) {
		fts_read_reg(FTS_REG_CHIP_ID, &chipid_in_chip);
		fts_read_reg(FTS_REG_VENDOR_ID, &vendorid_in_chip);
		fts_read_reg(FTS_REG_FW_VER, &fwver_in_chip);
		fts_read_reg(FTS_REG_LIC_VER, &lcdver_in_chip);
		if ((chipid_in_chip != 0) && (vendorid_in_chip != 0) && (fwver_in_chip != 0)) {
			FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x,lcd_version=%x .\n",
				  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
			break;
		}
		FTS_DEBUG("chip_id = %x,vendor_id =%x,fw_version=%x, lcd_version=%x .\n",
			  chipid_in_chip, vendorid_in_chip, fwver_in_chip, lcdver_in_chip);
		msleep(20);
	}

	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Focal");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_FOCAL;

	cdev->ic_tpinfo.chip_part_id = chipid_in_chip;
	cdev->ic_tpinfo.module_id = vendorid_in_chip;
	cdev->ic_tpinfo.chip_ver = 0;
	cdev->ic_tpinfo.firmware_ver = fwver_in_chip;
	cdev->ic_tpinfo.display_ver = lcdver_in_chip;
	cdev->ic_tpinfo.i2c_type = 0;
	cdev->ic_tpinfo.i2c_addr = 0x38;

	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_gesture_enable = ts_data->ztec.is_wakeup_gesture;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	ts_data->ztec.is_set_wakeup_in_suspend = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_wakeup_gesture = enable;
	}

	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
static int tpd_get_singleaodgesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_single_aod_enable = ts_data->ztec.is_single_aod;
	FTS_INFO("%s: enter!, ts_data->ztec.is_single_aod=%d", __func__, ts_data->ztec.is_single_aod);
	FTS_INFO("%s: enter!, cdev->b_single_aod_enable=%d", __func__, cdev->b_single_aod_enable);
	return 0;
}

static int tpd_set_singleaodgesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;
	FTS_INFO("%s: enter!, enable=%d", __func__, enable);
	ts_data->ztec.is_single_aod = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_single_aod = enable;
		ts_data->ztec.is_single_tap = (ts_data->ztec.is_single_aod || ts_data->ztec.is_single_fp) ? 5 : 0;
	}
	FTS_INFO("ts_data->ztec.is_single_fp=%d", ts_data->ztec.is_single_fp);
	FTS_INFO("ts_data->ztec.is_single_aod=%d", ts_data->ztec.is_single_aod);
	FTS_INFO("ts_data->ztec.is_single_tap=%d", ts_data->ztec.is_single_tap);
	return 0;
}

static int tpd_get_singlefpgesture(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->b_single_tap_enable = ts_data->ztec.is_single_fp;
	FTS_INFO("%s: enter!, ts_data->ztec.is_single_fp=%d", __func__, ts_data->ztec.is_single_fp);
	FTS_INFO("%s: enter!, cdev->b_single_tap_enable=%d", __func__, cdev->b_single_tap_enable);
	return 0;
}

static int tpd_set_singlefpgesture(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;
	FTS_INFO("%s: enter!, enable=%d", __func__, enable);
	ts_data->ztec.is_single_fp = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_single_fp = enable;
		ts_data->ztec.is_single_tap = (ts_data->ztec.is_single_aod || ts_data->ztec.is_single_fp) ? 5 : 0;
	}
	FTS_INFO("ts_data->ztec.is_single_fp=%d", ts_data->ztec.is_single_fp);
	FTS_INFO("ts_data->ztec.is_single_aod=%d", ts_data->ztec.is_single_aod);
	FTS_INFO("ts_data->ztec.is_single_tap=%d", ts_data->ztec.is_single_tap);
	return 0;
}

static int tpd_set_one_key(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	ts_data->ztec.is_set_onekey_in_suspend = enable;
	if (fts_data->suspended) {
		FTS_ERROR("%s: error, change set in suspend!", __func__);
	} else {
		ts_data->ztec.is_one_key = enable;
	}

	return 0;
}

static int tpd_get_one_key(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	cdev->one_key_enable = ts_data->ztec.is_one_key;

	return 0;
}
#endif

static bool fts_suspend_need_awake(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)cdev->private;

	if (!ts_data->ic_info.is_incell)
		return false;
	if (!cdev->tp_suspend_write_gesture &&
		(ts_data->fw_loading || ts_data->gesture_mode)) {
		FTS_INFO("tp suspend need awake.\n");
		return true;
	} else {
		FTS_INFO("tp suspend dont need awake.\n");
		return false;
	}
}

static int fts_tp_fw_upgrade(struct ztp_device *cdev, char *fw_name, int fwname_len)
{
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev = ts_data->input_dev;

	mutex_lock(&input_dev->mutex);
	fts_upgrade_bin(NULL, 0);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

int fts_tp_suspend(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_suspend(ts_data->dev);
	return 0;
}

int fts_tp_resume(void *fts_data)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

	fts_ts_resume(ts_data->dev);
	return 0;
}

static int fts_tp_suspend_show(struct ztp_device *cdev)
{
	cdev->tp_suspend = fts_data->suspended;
	return cdev->tp_suspend;
}

static int fts_set_tp_suspend(struct ztp_device *cdev, u8 suspend_node, int enable)
{
	if (enable)
		change_tp_state(LCD_OFF);
	else
		change_tp_state(LCD_ON);
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_INFO("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", fts_tptest_result, tdata->node.tx_num,
			tdata->node.rx_num, 0);
	num_read_chars = i_len;
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}

	input_dev = ts_data->input_dev;
	snprintf(g_fts_ini_filename, sizeof(g_fts_ini_filename), "fts_test_sensor_%02x.ini",
			tpd_cdev->ic_tpinfo.module_id);
	FTS_TEST_DBG("g_fts_ini_filename:%s.", g_fts_ini_filename);

	mutex_lock(&input_dev->mutex);
	fts_irq_disable();

#if FTS_ESDCHECK_EN
	fts_esdcheck_switch(ts_data, DISABLE);
#endif

	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
	} else {
		fts_test_entry(g_fts_ini_filename);
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
	}
#if FTS_ESDCHECK_EN
	 fts_esdcheck_switch(ts_data, ENABLE);
#endif

	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int fts_headset_state_show(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->headset_state = ts_data->headset_mode;
	return cdev->headset_state;
}

static int fts_set_headset_state(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;

	ts_data->headset_mode = enable;
	FTS_INFO("%s: headset_state = %d.\n", __func__, ts_data->headset_mode);
	if (!ts_data->suspended) {
		fts_ex_mode_switch(MODE_HEADSET, ts_data->headset_mode);
	}
	return ts_data->headset_mode;
}

static int fts_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;

	cdev->display_rotation = mrotation;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("%s: display_rotation = %d.\n", __func__, cdev->display_rotation);
	switch (cdev->display_rotation) {
		case mRotatin_0:
			ret = fts_write_reg(FTS_REG_MROTATION, 0);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_90:
			ret = fts_write_reg(FTS_REG_MROTATION, 1);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_180:
			ret = fts_write_reg(FTS_REG_MROTATION, 0);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		case mRotatin_270:
			ret = fts_write_reg(FTS_REG_MROTATION, 2);
			if (ret < 0) {
				FTS_ERROR("%s write display_rotation fail", __func__);
			}
			break;
		default:
			break;
	}
	return cdev->display_rotation;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret = 0;

	if (tp_report_rate_level > 3)
		tp_report_rate_level = 3;
	ts_data->ztec.tp_report_rate = tp_report_rate_level;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no set report rate, return now");
		return -EINVAL;
	} else {
		/*0:in tp report mode->in 120Hz;
		  1:in tp report mode->in 240Hz;
		  2:in tp report mode->in 360Hz;
		  3:in tp report mode->in 480Hz;
		  */
		switch (tp_report_rate_level) {
			case tp_freq_120Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x0C);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_240Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x18);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_360Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x24);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			case tp_freq_480Hz:
				ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x30);
				if (ret < 0) {
					FTS_ERROR("%s write report_rate fail", __func__);
				}
				break;
			default:
				break;
		}
	}

	return 0;
}

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->tp_report_rate = ts_data->ztec.tp_report_rate;

	return 0;
}

static int tpd_set_sensibility(struct ztp_device *cdev, u8 enable)
{
	int retval = 0;
	struct fts_ts_data *ts_data = fts_data;

	if (ts_data->sensibility_level  == enable)  {
		FTS_INFO("same sensibility level,return");
		return 0;
	}

	ts_data->sensibility_level = enable;
	cdev->sensibility_level = enable;
	if (ts_data->suspended)
		return 0;
	FTS_INFO("%s: sensibility_level = %d.\n", __func__, cdev->sensibility_level);
	retval = fts_write_reg(FTS_REG_SENSIBILITY_MODE_EN, ts_data->sensibility_level - 1);
	if (retval < 0) {
		FTS_ERROR("%s write sensibility_level fail", __func__);
	}
	return retval;
}

static int tpd_set_play_game(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;
	int ret;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no set play game, return now");
		return -EINVAL;
	} else {
		ts_data->ztec.is_play_game = enable;
		FTS_INFO("play_game mode is %d", enable);
		if (enable) {
			ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x30);
			ret = fts_write_reg(FTS_REG_GAME_MODE, 1);
			FTS_INFO("enter_play_game success\n");
		} else {
			ret = fts_write_reg(FTS_REG_REPORT_RATE, 0x18);
			ret = fts_write_reg(FTS_REG_GAME_MODE, 0);
			FTS_INFO("leave_play_game success!\n");
		}
	}

	return 0;
}

static int tpd_get_play_game(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->play_game_enable = ts_data->ztec.is_play_game;

	return 0;
}

static int tpd_set_palm_mode(struct ztp_device *cdev, int enable)
{
	struct fts_ts_data *ts_data = fts_data;

	ts_data->ztec.is_palm_mode = enable;
	FTS_INFO("palm_mode is %d", enable);

	return 0;
}

static int tpd_get_palm_mode(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;

	cdev->palm_mode_en = ts_data->ztec.is_palm_mode;

	return 0;
}

static int fts_charger_state_notify(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = fts_data;
	bool charger_mode_old = ts_data->charger_mode;

	ts_data->charger_mode = cdev->charger_mode;

	if (!ts_data->suspended && (ts_data->charger_mode != charger_mode_old)) {
		FTS_INFO("write charger mode:%d", ts_data->charger_mode);
		fts_ex_mode_switch(MODE_CHARGER, ts_data->charger_mode);
	}
	return 0;
}

static int tpd_fts_shutdown(struct ztp_device *cdev)
{
	struct fts_ts_data *ts_data = (struct fts_ts_data *)fts_data;

#if FTS_POINT_REPORT_CHECK_EN
	cancel_delayed_work_sync(&fts_data->prc_work);
#endif
	fts_ts_suspend(ts_data->dev);
	return 0;
}

static int fts_tp_get_diffdata(int *data, int byte_num)
{
	int ret = 0;
	u8 old_mode = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_TEST_FUNC_ENTER();
	ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &old_mode);
	if (ret < 0) {
		FTS_TEST_ERROR("read reg06 fail\n");
		goto test_err;
	}
	ret =  fts_test_write_reg(FACTORY_REG_DATA_SELECT, 0x01);
	if (ret < 0) {
		FTS_TEST_ERROR("write 1 to reg06 fail\n");
		goto restore_reg;
	}
	/* tart Scanning */
	ret = start_scan();
	if (ret < 0) {
		FTS_TEST_ERROR("Failed to Scan ...");
		return ret;
	}
	/* read rawdata */
	if (tdata->func->hwtype == IC_HW_INCELL) {
		val = 0xAD;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
	} else if (tdata->func->hwtype == IC_HW_MC_SC) {
		val = 0xAA;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
	} else {
		val = 0x0;
		addr = FACTORY_REG_RAWDATA_SADDR_SC;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
	}
	/* read diffdata */
	ret = read_rawdata(tdata, addr, val, rawdata_addr, byte_num, data);
	if (ret < 0) {
		FTS_TEST_ERROR("read diffdata fail");
		goto restore_reg;
	}
restore_reg:
	ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, old_mode);
	if (ret < 0) {
		FTS_TEST_ERROR("restore reg06 fail");
	}
test_err:
	FTS_TEST_FUNC_EXIT();
	return ret;
}

static int fts_tp_get_rawdata(int *data, int byte_num) {
	int ret = 0;
	u8 val = 0;
	u8 addr = 0;
	u8 rawdata_addr = 0;
	struct fts_test *tdata = fts_ftest;

	FTS_TEST_FUNC_ENTER();
	/* tart Scanning */
	ret = start_scan();
	if (ret < 0) {
		FTS_TEST_ERROR("Failed to Scan ...");
		return ret;
	}
	/* read rawdata */
	if (tdata->func->hwtype == IC_HW_INCELL) {
		val = 0xAD;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR;
	} else if (tdata->func->hwtype == IC_HW_MC_SC) {
		val = 0xAA;
		addr = FACTORY_REG_LINE_ADDR;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
	} else {
		val = 0x0;
		addr = FACTORY_REG_RAWDATA_SADDR_SC;
		rawdata_addr = FACTORY_REG_RAWDATA_ADDR_SC;
	}
	/* read rawdata */
	ret = read_rawdata(tdata, addr, val, rawdata_addr, byte_num, data);
	if (ret < 0) {
		FTS_TEST_ERROR("read rawdata failed");
		return ret;
	}
	return ret;
}
static int fts_data_request(struct ztp_device *cdev, int *frame_data_words,
	enum tp_test_type  test_type, int byte_num)
{
	int  ret = 0;

	switch (test_type) {
	case RAWDATA_TEST:
		ret = fts_tp_get_rawdata(frame_data_words, byte_num);
		if (ret) {
			FTS_ERROR("Get raw data failed %d", ret);
		}
		break;
	case DELTA_TEST:
		ret = fts_tp_get_diffdata(frame_data_words, byte_num);
		if (ret) {
			FTS_ERROR("Get diff data failed %d", ret);
		}
		break;
	default:
		FTS_ERROR("%s:the Para is error!", __func__);
		ret = -1;
	}

	return ret;
}

static int  fts_testing_delta_raw_report(struct ztp_device *cdev, u8 num_of_reports)
{
	int *frame_data_words = NULL;
	int retval = 0;
	int len = 0;
	int i = 0;
	u8 tx_num = 0;
	u8 rx_num = 0;
	u8 col = 0;
	u8 row = 0;
	u8 idx = 0;
	struct fts_ts_data *ts_data = fts_data;
	struct input_dev *input_dev;

	if (ts_data->suspended) {
        FTS_INFO("In suspend, no proc, return now");
        return -EINVAL;
    }

	input_dev = ts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_irq_disable();

#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
    fts_esdcheck_switch(ts_data, DISABLE);
#endif

	retval = fts_write_reg(0xEE,1);/* disable Auto Clb */
	if (retval < 0) {
		FTS_TEST_ERROR("disable auto clb fail, ret=%d", retval);
		goto err_disable_clb_fail;
	}
	retval = enter_factory_mode();
	if (retval < 0) {
		FTS_TEST_ERROR("enter factory mode fail, ret=%d", retval);
		goto exit;
	}
	retval = fts_read_reg(FACTORY_REG_CHX_NUM, &tx_num);
	if (retval < 0) {
		FTS_TEST_ERROR("get tx_num fail, ret=%d", retval);
		goto exit;
	}
	retval = fts_read_reg(FACTORY_REG_CHY_NUM, &rx_num);
	if (retval < 0) {
		FTS_TEST_ERROR("get rx_num fail, ret=%d", retval);
		goto exit;
	}

	row = tx_num;
	col = rx_num;
	frame_data_words = kcalloc((row * col), sizeof(int), GFP_KERNEL);
	if (frame_data_words ==  NULL) {
		FTS_ERROR("Failed to allocate frame_data_words mem");
		retval = -1;
		goto MEM_ALLOC_FAILED;
	}
	for (idx = 0; idx < num_of_reports; idx++) {
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"frame: %d, TX:%d  RX:%d\n", idx, row, col);
		retval = fts_data_request(cdev, frame_data_words, RAWDATA_TEST, tx_num * rx_num * 2);
		if (retval < 0) {
			FTS_ERROR("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"RawData:\n");
		for (i = 0; i < row * col; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % col == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
		retval = fts_data_request(cdev, frame_data_words, DELTA_TEST, tx_num * rx_num * 2);
		if (retval < 0) {
			FTS_ERROR("data_request failed!");
			goto DATA_REQUEST_FAILED;
		}
		len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"DiffData:\n");
		for (i = 0; i < row * col; i++) {
			len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len,
				"%5d,", frame_data_words[i]);
			if ((i + 1) % col == 0)
				len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n");
		}
	}

DATA_REQUEST_FAILED:
	len += snprintf((char *)(cdev->tp_firmware->data + len), RT_DATA_LEN * 10 - len, "\n\n");
	fts_reset_proc(200);
	FTS_INFO("get tp delta raw data end!");
	kfree(frame_data_words);
	frame_data_words = NULL;
MEM_ALLOC_FAILED:
exit:
	enter_work_mode();
err_disable_clb_fail:
#if defined(FTS_ESDCHECK_EN) && (FTS_ESDCHECK_EN)
   fts_esdcheck_switch(ts_data, ENABLE);
#endif
    fts_irq_enable();
    mutex_unlock(&input_dev->mutex);
	FTS_TEST_FUNC_EXIT();
	return retval;
}

static int fts_get_noise(struct ztp_device *cdev)
{
	int ret =0;

	if(tp_alloc_tp_firmware_data(10 * RT_DATA_LEN)) {
		FTS_ERROR(" alloc tp firmware data fail");
		return -ENOMEM;
	}
	ret = fts_testing_delta_raw_report(cdev, 5);
	if (ret) {
		FTS_ERROR( "%s:get_noise failed\n",  __func__);
		return ret;
	} else {
		FTS_INFO("%s:get_noise success\n",  __func__);
	}
	return 0;
}


int tpd_register_fw_class(struct fts_ts_data *data)
{
	tpd_cdev->private = (void *)data;
	tpd_cdev->get_tpinfo = tpd_init_tpinfo;

	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;

#ifdef CONFIG_TOUCHSCREEN_UFP_MAC
	tpd_cdev->get_singleaod = tpd_get_singleaodgesture;
	tpd_cdev->set_singleaod = tpd_set_singleaodgesture;

	tpd_cdev->get_singletap = tpd_get_singlefpgesture;
	tpd_cdev->set_singletap = tpd_set_singlefpgesture;
	tpd_cdev->set_one_key = tpd_set_one_key;
	tpd_cdev->get_one_key = tpd_get_one_key;
#endif

	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;
	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;
	tpd_cdev->tp_fw_upgrade = fts_tp_fw_upgrade;
	tpd_cdev->tp_suspend_show = fts_tp_suspend_show;
	tpd_cdev->set_tp_suspend = fts_set_tp_suspend;
	tpd_cdev->tpd_suspend_need_awake = fts_suspend_need_awake;
	tpd_cdev->set_display_rotation = fts_set_display_rotation;
	tpd_cdev->headset_state_show = fts_headset_state_show;
	tpd_cdev->set_headset_state = fts_set_headset_state;
	tpd_cdev->set_sensibility = tpd_set_sensibility;
	tpd_cdev->tp_data = data;
	tpd_cdev->tp_resume_func = fts_tp_resume;
	tpd_cdev->tp_suspend_func = fts_tp_suspend;
	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;
	tpd_cdev->tpd_shutdown = tpd_fts_shutdown;
	tpd_cdev->tp_palm_mode_read = tpd_get_palm_mode;
	tpd_cdev->tp_palm_mode_write = tpd_set_palm_mode;
	tpd_cdev->get_noise = fts_get_noise;
	tpd_init_tpinfo(tpd_cdev);
	tpd_cdev->max_x = data->pdata->x_max;
	tpd_cdev->max_y = data->pdata->y_max;
	data->sensibility_level = 1;
	snprintf(g_fts_ini_filename, sizeof(g_fts_ini_filename), "fts_test_sensor_%02x.ini",
		tpd_cdev->ic_tpinfo.module_id);
	tpd_cdev->charger_state_notify = fts_charger_state_notify;
	queue_delayed_work(tpd_cdev->tpd_wq, &tpd_cdev->charger_work, msecs_to_jiffies(5000));

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	get_fts_module_info_from_lcd();
	zlog_tp_dev.device_name = fts_vendor_name;
	zlog_tp_dev.ic_name = "focal_tp";
	TPD_ZLOG("device_name:%s, ic_name: %s.", zlog_tp_dev.device_name, zlog_tp_dev.ic_name);
#endif
	return 0;
}

