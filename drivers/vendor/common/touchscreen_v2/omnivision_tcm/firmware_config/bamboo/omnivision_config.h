
#ifndef _OMNIVISION_CONFIG_H_
#define _OMNIVISION_CONFIG_H_

#define OVT_TCM_MODULE1_ID                         0x0001
#define OVT_TCM_MODULE2_ID                         0x0002
#define OVT_TCM_MODULE3_ID                         0x0003

#define OVT_TCM_MODULE1_LCD_NAME                   "easyquick"
#define OVT_TCM_MODULE2_LCD_NAME                   "Unknown"
#define OVT_TCM_MODULE3_LCD_NAME                   "Unknown"

#define TX_NUM_MAX	36
#define RX_NUM_MAX	16
/*default i2c*/
#define USE_SPI_BUS
#ifdef USE_SPI_BUS
/*define use spi num*/
#define SPI_NUM                                    3
#endif

#define OVT_TCM_REPORT_BY_ZTE_ALGO

#define OVT_TCM_LCD_OPERATE_TP_RESET

#define OVT_DEFAULT_FW_IMAGE_NAME "ovt_boe_default_firmware.img"

#endif /* _OMNIVISION_CONFIG_H_ */
