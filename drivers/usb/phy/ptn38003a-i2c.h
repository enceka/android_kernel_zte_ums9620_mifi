#ifndef __REDRIVER_I2C_PTN38003A__
#define __REDRIVER_I2C_PTN38003A__

#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#define PTN38003A_CHIP_ID	0x00
#define PTN38003A_MODE_CONTROL 0x04
#define PTN38003A_DP_LINK_CONTROL_AND_STATUS 0x06

#define PTN38003A_OPERATION_MODE_32_MASK 0x01
#define PTN38003A_OPERATION_MODE_MASK 0x07
#define PTN38003A_AUTO_ORIENT_EN_MASK 0x80
#define PTN38003A_DP_LANE_COUNT_MASK 0x0c
#define PTN38003A_DP_LINK_RATE_MASK 0x03

#define PTN38003A_DEEP_POWER_SAVING 0x00
#define PTN38003A_OPERATION_MODE_32 0x01
#define PTN38003A_MODE_32_AND_2LANE_DP 0X02
#define PTN38003A_MODE_4LANE_DP 0x03
#define PTN38003A_AUTO_ORIENT_EN 0X80

struct redriver_ptn38003a {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
};

#if IS_ENABLED(CONFIG_SPRD_REDRIVER_PTN38003A)
extern int ptn38003a_mode_usb32_set(unsigned int enable);
int ptn38003a_dp_lane_set(unsigned int value);
int ptn38003a_dp_link_rate_set(unsigned int value);
int ptn38003a_dp_mode_set(unsigned int value);
#else
static inline int ptn38003a_mode_usb32_set(unsigned int enable) { return 0; }
static inline void ptn38003a_dp_lane_set(unsigned int value) {}
static inline void ptn38003a_dp_link_rate_set(unsigned int value) {}
static inline void ptn38003a_dp_mode_set(unsigned int value) {}
#endif

#endif
