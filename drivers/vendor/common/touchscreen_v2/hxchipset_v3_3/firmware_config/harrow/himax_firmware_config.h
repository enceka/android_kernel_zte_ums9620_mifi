
/************************************************************************
*
* File Name: himax_firmware_config.h
*
*  *   Version: v1.0
*
************************************************************************/
#ifndef _HIMAX_FIRMWARE_CONFIG_H_
#define _HIMAX_FIRMWARE_CONFIG_H_

/********************** Upgrade ***************************

  auto upgrade, please keep enable
*********************************************************/
/* #define HX_HIGH_SENSE */
/* #define HX_USB_DETECT_GLOBAL
#define HEADLINE_MODE
#define HX_FIX_TOUCH_INFO
#define HX_DISPLAY_ROTATION
#define HX_EDGE_LIMIT
#define HX_PINCTRL_EN
#define HX_SENSIBILITY */

/*===========Himax Option function=============*/
#define HX_RST_PIN_FUNC
#define HX_EXCP_RECOVERY



/*#define HX_NEW_EVENT_STACK_FORMAT*/
/*#define HX_BOOT_UPGRADE*/
#define HX_SMART_WAKEUP
/*#define HX_GESTURE_TRACK*/
#define HX_RESUME_SEND_CMD	/*Need to enable on TDDI chipset*/
/*#define HX_HIGH_SENSE*/
/*#define HX_PALM_REPORT*/
/*#define HX_USB_DETECT_GLOBAL*/
#define HX_RW_FILE
#define HX_HEADSET_MODE

/* for MTK special platform.If turning on,
 * it will report to system by using specific format.
 */
/* #define HX_REPORT_BY_ZTE_ALGO */
/*#define HX_PROTOCOL_A*/
#define HX_PROTOCOL_B_3PA

#define HX_ZERO_FLASH

/*system suspend-chipset power off,
 *oncell chipset need to enable the definition
 */
/*#define HX_RESUME_HW_RESET*/

/* Sample code for TP load before LCM
 */
/*#define HX_TP_TRIGGER_LCM_RST*/

/*#define HX_PARSE_FROM_DT*/

/*Enable this if testing suspend/resume
 *on nitrogen8m
 */
/*#define HX_CONFIG_DRM_PANEL*/

/*used for self test get dsram fail in stress test*/
/*#define HX_STRESS_SELF_TEST*/

/*=============================================*/

/* Enable it if driver go into suspend/resume twice */
/*#undef HX_CONFIG_FB*/

/* Enable it if driver go into suspend/resume twice */
/*#undef HX_CONFIG_DRM*/

#if defined(HX_FIX_TOUCH_INFO)
enum fix_touch_info {
	FIX_HX_RX_NUM = 36,
	FIX_HX_TX_NUM = 18,
	FIX_HX_BT_NUM = 0,
	FIX_HX_MAX_PT = 10,
	FIX_HX_XY_REVERSE = false,
	FIX_HX_INT_IS_EDGE = true,
	FIX_HX_PEN_FUNC = false,
#if defined(HX_TP_PROC_2T2R)
	FIX_HX_RX_NUM_2 = 0,
	FIX_HX_TX_NUM_2 = 0,
#endif
};
#endif

enum himax_vendor_id {
	HX_VENDOR_ID_0	= 0xA1,
	HX_VENDOR_ID_1,
	HX_VENDOR_ID_2,
	HX_VENDOR_ID_3,
	HX_VENDOR_ID_MAX		= 0xFF,
};

/*
 * Numbers of modules support
 */
#define HXTS_VENDOR_0_NAME	"lectron"
#define HXTS_VENDOR_1_NAME	"unknown"
#define HXTS_VENDOR_2_NAME	"unknown"
#define HXTS_VENDOR_3_NAME	"unknown"
#ifdef HX_ZERO_FLASH
/* this macro need be configured refer to module*/
#define HXTS_DEFAULT_FIRMWARE     "hxtp_6_67_default_common_firmware"
#endif

#endif
