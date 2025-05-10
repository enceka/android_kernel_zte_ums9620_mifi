#ifndef _CHIPONE_CONFIG_H_
#define _CHIPONE_CONFIG_H_

#define CTS_MODULE1_ID                         0x01
#define CTS_MODULE2_ID                         0
#define CTS_MODULE3_ID                         0

#define CTS_MODULE1_LCD_NAME                   "easyquick"
#define CTS_MODULE2_LCD_NAME                   "Unknown"
#define CTS_MODULE3_LCD_NAME                   "Unknown"

/*default i2c*/
#define USE_SPI_BUS
#ifdef USE_SPI_BUS
/*define use spi num*/
#define SPI_NUM                                    3
#endif

#define CTS_REPORT_BY_ZTE_ALGO
/* #define CTS_LCD_OPERATE_TP_RESET */

#define CONFIG_CTS_CHARGER_DETECT

#define CFG_CTS_HEADSET_DETECT

#define CFG_CTS_ROTATION

#define CFG_CTS_GESTURE

#define CFG_USE_DEFAULT_ROWS_COLS
#ifdef CFG_USE_DEFAULT_ROWS_COLS
#define CFG_DEFAULT_ROWS    32
#define CFG_DEFAULT_COLS    18
#endif

#endif /* _CHIPONE_CONFIG_H_ */