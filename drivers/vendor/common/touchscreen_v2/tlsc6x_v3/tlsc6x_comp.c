/*
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION		DATE			AUTHOR
 *
 */
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/input/mt.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/suspend.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include "tlsc6x_main.h"
#include "chip3535_update.h"
#include "chip3536_update.h"
#ifdef TLSC_GESTRUE
#include "chip3535_gesture_binlib.h"
#endif
#include "chip3535_rawdata_test.h"
#include "chip3536_rawdata_test.h"

#ifdef TLSC_BUILDIN_BOOT
#include "tlsc6x_boot.h"
#endif

unsigned int g_tlsc6x_cfg_ver = 0;
unsigned int g_tlsc6x_boot_ver = 0;
unsigned int g_needKeepRamCode = 0;
unsigned short g_tlsc6x_chip_code = 0;

unsigned int g_mccode = 0;	/* 0:3535, 1:3536 */

struct i2c_client *g_tlsc6x_client;
extern struct mutex i2c_rw_access;
extern char tlsc6x_vendor_name[];

#ifdef TLSC_AUTO_UPGRADE
static int new_idx_active = -1;
#endif
int tlsc6x_load_ext_binlib(u8 *pcode, u16 len);

/* Telink CTP */
unsigned int MTK_TXRX_BUF;
unsigned int CMD_ADDR;
unsigned int RSP_ADDR;

typedef struct __test_cmd_wr {
	/* offset 0; */
	unsigned char id;	/* cmd_id; */
	unsigned char idv;	/* inverse of cmd_id */
	unsigned short d0;	/* data 0 */
	unsigned short d1;	/* data 1 */
	unsigned short d2;	/* data 2 */
	/* offset 8; */
	unsigned char resv;	/* offset 8 */
	unsigned char tag;	/* offset 9 */
	unsigned short chk;	/* 16 bit checksum */
	unsigned short s2Pad0;	/*  */
	unsigned short s2Pad1;	/*  */
} ctp_test_wr_t;

typedef struct __test_cmd_rd {
	/* offset 0; */
	unsigned char id;	/* cmd_id; */
	unsigned char cc;	/* complete code */
	unsigned short d0;	/* data 0 */
	unsigned short sn;	/* session number */
	unsigned short chk;	/* 16 bit checksum */
} ctp_test_rd_t;
#define DIRECTLY_MODE   (0x0)
#define DEDICATE_MODE   (0x1)
#define MAX_TRX_LEN (64)	/* max IIC data length */
/* #define CMD_ADDR    (0xb400) */
/* #define RSP_ADDR    (0xb440) */
/* #define MTK_TXRX_BUF    (0xcc00)  // 1k, buffer used for memory r &w */

#define LEN_CMD_CHK_TX  (10)
#define LEN_CMD_PKG_TX  (16)

#define LEN_RSP_CHK_RX  (8)
#define MAX_BULK_SIZE    (1024)
unsigned short tl_target_cfg[102];
unsigned short tl_buf_tmpcfg[102];
/* to direct memory access mode */
unsigned char cmd_2dma_42bd[6] = { /*0x42, 0xbd, */ 0x28, 0x35, 0xc1, 0x00, 0x35, 0xae };

/* in directly memory access mode */
/* RETURN:0->pass else->fail */
int tlsc6x_read_bytes_u16addr_sub(struct i2c_client *client, u16 addr, u8 *rxbuf, u16 len)
{
	int err = 0;
	int retry = 0;
	u16 offset = 0;
	u8 buffer[2];

	struct i2c_msg msgs[2] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = 2,	/* 16bit memory address */
		 .buf = buffer,
		 },
		{
		 .addr = client->addr,
		 .flags = I2C_M_RD,
		 },
	};

	if (rxbuf == NULL) {
		return -EPERM;
	}
	/* mutex_lock(&g_mutex_i2c_access); */

	while (len > 0) {
		buffer[0] = (u8) ((addr + offset) >> 8);
		buffer[1] = (u8) (addr + offset);

		msgs[1].buf = &rxbuf[offset];
		if (len > MAX_TRX_LEN) {
			len -= MAX_TRX_LEN;
			msgs[1].len = MAX_TRX_LEN;
		} else {
			msgs[1].len = len;
			len = 0;
		}

		retry = 0;
		while (tlsc6x_i2c_read_sub(client, buffer, 2, &rxbuf[offset], msgs[1].len) < 0) {
			if (retry++ == 3) {
				err = -1;
				break;
			}
		}
		offset += MAX_TRX_LEN;
		if (err < 0) {
			break;
		}
	}

	/* mutex_unlock(&g_mutex_i2c_access); */

	return err;
}

int tlsc6x_read_bytes_u16addr(struct i2c_client *client, u16 addr, u8 *rxbuf, u16 len)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_read_bytes_u16addr_sub(client, addr, rxbuf, len);
	mutex_unlock(&i2c_rw_access);
	return ret;
}

/* in directly memory access mode */
/* RETURN:0->pass else->fail */
int tlsc6x_write_bytes_u16addr_sub(struct i2c_client *client, u16 addr, u8 *txbuf, u16 len)
{
	u8 buffer[MAX_TRX_LEN];
	u16 offset = 0;
	u8 retry = 0;
	int err = 0;

	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.buf = buffer,
	};

	if (txbuf == NULL) {
		return -EPERM;
	}
	/* mutex_lock(&g_mutex_i2c_access); */

	while (len) {
		buffer[0] = (u8) ((addr + offset) >> 8);
		buffer[1] = (u8) (addr + offset);

		if (len > (MAX_TRX_LEN - 2)) {	/* (sizeof(addr)+payload) <= MAX_TRX_LEN */
			memcpy(&buffer[2], &txbuf[offset], (MAX_TRX_LEN - 2));
			len -= (MAX_TRX_LEN - 2);
			offset += (MAX_TRX_LEN - 2);
			msg.len = MAX_TRX_LEN;
		} else {
			memcpy(&buffer[2], &txbuf[offset], len);
			msg.len = len + 2;
			len = 0;
		}

		retry = 0;
		while (tlsc6x_i2c_write_sub(client, buffer, msg.len) < 0) {
			if (retry++ == 3) {
				err = -1;
				break;
			}
		}
		if (err < 0) {
			break;
		}
	}

	/* mutex_unlock(&g_mutex_i2c_access); */

	return err;
}

int tlsc6x_write_bytes_u16addr(struct i2c_client *client, u16 addr, u8 *txbuf, u16 len)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_write_bytes_u16addr_sub(client, addr, txbuf, len);
	mutex_unlock(&i2c_rw_access);
	return ret;
}

/* <0 : i2c error */
/* 0: direct address mode */
/* 1: protect mode */
int tlsc6x_get_i2cmode(void)
{
	u8 regData[4];

	if (tlsc6x_read_bytes_u16addr_sub(g_tlsc6x_client, 0x01, regData, 3)) {
		return -EPERM;
	}

	if (((u8) (g_tlsc6x_client->addr) == (regData[0] >> 1)) && (regData[2] == 0X01)) {
		return DIRECTLY_MODE;
	}

	return DEDICATE_MODE;
}

/* 0:successful */
int tlsc6x_set_dd_mode_sub(void)
{
	u8  uctemp = 0x66;
	int mod = -1;
	int retry = 0;

	if (tlsc6x_get_i2cmode() == DIRECTLY_MODE) {
		return 0;
	}

	while (retry++ < 5) {
		usleep_range(10000, 11000);
		tlsc6x_write_bytes_u16addr_sub(g_tlsc6x_client, 0x42bd, cmd_2dma_42bd, 6);
		msleep(20);	/* most one sw-frame time */
		tlsc6x_write_bytes_u16addr_sub(g_tlsc6x_client, 0x40f, &uctemp, 1);
		mod = tlsc6x_get_i2cmode();
		if (mod == DIRECTLY_MODE) {
			break;
		}
	}
	tlsc_info("%s retry=%d, mod=%d", __func__, retry, mod);

	if (mod == DIRECTLY_MODE) {
		return 0;
	} else {
		return -EPERM;
	}
}

int tlsc6x_set_dd_mode(void)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_set_dd_mode_sub();
	mutex_unlock(&i2c_rw_access);
	return ret;
}

/* 0:successful */
int tlsc6x_set_nor_mode_sub(void)
{
	int mod = -1;
	int retry = 0;
	u8 reg = 0x05;

	while (retry++ < 5) {
		tlsc6x_write_bytes_u16addr_sub(g_tlsc6x_client, 0x03, &reg, 1);
		usleep_range(5000, 5500);
		mod = tlsc6x_get_i2cmode();
		if (mod == DEDICATE_MODE) {
			break;
		}
		msleep(50);
	}
	if (mod == DEDICATE_MODE) {
		return 0;
	} else {
		return -EPERM;
	}
}

int tlsc6x_set_nor_mode(void)
{
	int ret = 0;

	mutex_lock(&i2c_rw_access);
	ret = tlsc6x_set_nor_mode_sub();
	mutex_unlock(&i2c_rw_access);
	return ret;
}

/* ret=0 : successful */
/* write with read-back check, in dd mode */
static int tlsc6x_bulk_down_check(u8 *pbuf, u16 addr, u16 len)
{
	unsigned int j, k, retry;
	u8 rback[128];

	TLSC_FUNC_ENTER();
	while (len) {
		k = (len < 128) ? len : 128;
		retry = 0;
		do {
			rback[k - 1] = pbuf[k - 1] + 1;
			tlsc6x_write_bytes_u16addr(g_tlsc6x_client, addr, pbuf, k);
			tlsc6x_read_bytes_u16addr(g_tlsc6x_client, addr, rback, k);
			for (j = 0; j < k; j++) {
				if (pbuf[j] != rback[j]) {
					break;
				}
			}
			if (j >= k) {
				break;	/* match */
			}
		} while (++retry < 3);

		if (j < k) {
			break;
		}

		addr += k;
		pbuf += k;
		len -= k;
	}

	return (int)len;
}

static u16 tlsc6x_checksum_u16(u16 *buf, u16 length)
{
	unsigned short sum, len, i;

	sum = 0;

	len = length >> 1;

	for (i = 0; i < len; i++) {
		sum += buf[i];
	}

	return sum;
}

static u32 tlsc6x_checksumEx(u8 *buf, u16 length)
{
	u32 combChk;
	u16 k, check, checkEx;

	check = 0;
	checkEx = 0;
	for (k = 0; k < length; k++) {
		check += buf[k];
		checkEx += (u16) (k * buf[k]);
	}
	combChk = (checkEx << 16) | check;

	return combChk;

}

/* 0:successful */
static int tlsc6x_download_ramcode(u8 *pcode, u16 len)
{
	u8 dwr, retry;
	int ret = -2;
	int sig = 0;

	TLSC_FUNC_ENTER();
	if (tlsc6x_set_dd_mode()) {
		return -EPERM;
	}

	sig = (int) pcode[3];
	sig = (sig<<8) + (int) pcode[2];
	sig = (sig<<8) + (int) pcode[1];
	sig = (sig<<8) + (int) pcode[0];

	if (sig == 0x6d6f8008) {
		sig = 0;
		tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x8000, (u8 *) &sig, 4);
		if (sig == 0x6d6f8008) {
			return 0;
		}
	}

	dwr = 0x05;
	if (tlsc6x_bulk_down_check(&dwr, 0x0602, 1) == 0) {	/* stop mcu */
		dwr = 0x00;
		tlsc6x_bulk_down_check(&dwr, 0x0643, 1);	/* disable irq */
	} else {
		return -EPERM;
	}
	if (tlsc6x_bulk_down_check(pcode, 0x8000, len) == 0) {
		dwr = 0x88;
		retry = 0;
		do {
			ret = tlsc6x_write_bytes_u16addr(g_tlsc6x_client, 0x0602, &dwr, 1);
		} while ((++retry < 3) && (ret != 0));
	}

	msleep(30);

	return ret;
}

/* return 0=successful: send cmd and get rsp. */
static int tlsc6x_cmd_send(ctp_test_wr_t *ptchcw, ctp_test_rd_t *pcr)
{
	int ret;
	u32 retry;

	TLSC_FUNC_ENTER();
	retry = 0;
	tlsc6x_write_bytes_u16addr(g_tlsc6x_client, RSP_ADDR, (u8 *) &retry, 1);

	/* send command */
	ptchcw->idv = ~(ptchcw->id);
	ptchcw->tag = 0x35;
	ptchcw->chk = 1 + ~(tlsc6x_checksum_u16((u16 *) ptchcw, LEN_CMD_CHK_TX));
	ptchcw->tag = 0x30;
	ret = tlsc6x_write_bytes_u16addr(g_tlsc6x_client, CMD_ADDR, (u8 *) ptchcw, LEN_CMD_PKG_TX);
	if (ret) {
		goto exit;
	}
	ptchcw->tag = 0x35;
	ret = tlsc6x_write_bytes_u16addr(g_tlsc6x_client, CMD_ADDR + 9, (u8 *) &(ptchcw->tag), 1);
	if (ret) {
		goto exit;
	}
	/* polling rsp, the caller must init rsp buffer. */
	ret = -1;
	retry = 0;
	while (retry++ < 100) {	/* 2s */
		msleep(20);
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, RSP_ADDR, (u8 *) pcr, 1)) {
			break;
		}

		if (ptchcw->id != pcr->id) {
			continue;
		}
		/* msleep(50); */
		tlsc6x_read_bytes_u16addr(g_tlsc6x_client, RSP_ADDR, (u8 *) pcr, LEN_RSP_CHK_RX);
		if (!tlsc6x_checksum_u16((u16 *) pcr, LEN_RSP_CHK_RX)) {
			if ((ptchcw->id == pcr->id) && (pcr->cc == 0)) {
				ret = 0;
			}
		}
		break;
	}
exit:
	/* clean rsp buffer */
	/* retry = 0; */
	/* tlsc6x_write_bytes_u16addr(g_tlsc6x_client, RSP_ADDR, (u8*)&retry, 1); */

	return ret;

}

/* return 0=successful */
static int tlsc6x_read_burn_space(u8 *pdes, u16 adr, u16 len)
{
	int rsp;
	u32 left = len;
	u32 combChk, retry;
	ctp_test_wr_t m_cmd;
	ctp_test_rd_t m_rsp;

	TLSC_FUNC_ENTER();
	m_cmd.id = 0x31;
	m_cmd.resv = 0x03;
	while (left) {
		len = (left > MAX_BULK_SIZE) ? MAX_BULK_SIZE : left;

		m_cmd.d0 = adr;
		m_cmd.d1 = len;

		rsp = -1;
		retry = 0;
		while (retry++ < 3) {
			m_rsp.id = 0;
			if (tlsc6x_cmd_send(&m_cmd, &m_rsp) == 0X0) {
				tlsc6x_read_bytes_u16addr(g_tlsc6x_client, MTK_TXRX_BUF, pdes, len);
				combChk = tlsc6x_checksumEx(pdes, len);
				if (m_rsp.d0 == (unsigned short)combChk) {
					if (m_rsp.sn == (unsigned short)(combChk >> 16)) {
						rsp = 0;
						break;
					}
				}
			}
		}

		if (rsp < 0) {
			break;
		}
		left -= len;
		adr += len;
		pdes += len;
	}

	return rsp;
}

static int tlsc6x_write_burn_space(u8 *psrc, u16 adr, u16 len)
{
	int rsp = 0;
	u16 left = len;
	u32 retry, combChk;
	ctp_test_wr_t m_cmd;
	ctp_test_rd_t m_rsp;

	TLSC_FUNC_ENTER();
	m_cmd.id = 0x30;
	m_cmd.resv = 0x11;

	while (left) {
		len = (left > MAX_BULK_SIZE) ? MAX_BULK_SIZE : left;
		combChk = tlsc6x_checksumEx(psrc, len);

		m_cmd.d0 = adr;
		m_cmd.d1 = len;
		m_cmd.d2 = (u16) combChk;
		m_cmd.s2Pad0 = (u16) (combChk >> 16);

		rsp = -1;	/* avoid dead loop */
		retry = 0;
		while (retry < 3) {
			tlsc6x_write_bytes_u16addr(g_tlsc6x_client, MTK_TXRX_BUF, psrc, len);
			m_rsp.id = 0;
			rsp = tlsc6x_cmd_send(&m_cmd, &m_rsp);
			if (rsp < 0) {
				if ((m_rsp.d0 == 0X05) && (m_rsp.cc == 0X09)) {	/* fotal error */
					break;
				}
				retry++;
			} else {
				left -= len;
				adr += len;
				psrc += len;
				break;
			}
		}

		if (rsp < 0) {
			break;
		}
	}

	return (!left) ? 0 : -1;
}

static int is_valid_cfg_data(u16 *ptcfg)
{
	if (ptcfg == NULL) {
		return 0;
	}

	if ((u8) ((ptcfg[53] >> 1) & 0x7f) != (u8) (g_tlsc6x_client->addr)) {
		return 0;
	}

	if (tlsc6x_checksum_u16(ptcfg, 204)) {
		return 0;
	}

	return 1;
}

#ifdef TLSC_AUTO_UPGRADE
static int tlsc6x_tpcfg_ver_comp(unsigned short *ptcfg)
{
	unsigned int u32tmp;
	unsigned short vnow, vbuild;

	TLSC_FUNC_ENTER();
	if (g_tlsc6x_cfg_ver == 0) {	/* no available version information */
		return 0;
	}

	if (is_valid_cfg_data(ptcfg) == 0) {
		return 0;
	}

	u32tmp = ptcfg[1];
	u32tmp = (u32tmp << 16) | ptcfg[0];
	if ((g_tlsc6x_cfg_ver & 0x3ffffff) != (u32tmp & 0x3ffffff)) {
		return 0;
	}

	vnow = (g_tlsc6x_cfg_ver >> 26) & 0x3f;
	vbuild = (u32tmp >> 26) & 0x3f;
	tlsc_info("vnow: 0x%x,vbuild: 0x%x", vnow, vbuild);
	if (vbuild <= vnow) {
		return 0;
	}

	return 1;
}
#endif
static int tlsc6x_tpcfg_ver_comp_weak(unsigned short *ptcfg)
{
	unsigned int u32tmp;

	TLSC_FUNC_ENTER();
	if (g_tlsc6x_cfg_ver == 0) {	/* no available version information */
		tlsc_err("Tlsc6x:cfg_ver is null!\n");
		return 0;
	}

	if (is_valid_cfg_data(ptcfg) == 0) {
		tlsc_err("Tlsc6x:cfg_data is invalid!\n");
		return 0;
	}

	u32tmp = ptcfg[1];
	u32tmp = (u32tmp << 16) | ptcfg[0];
	if ((g_tlsc6x_cfg_ver & 0x3ffffff) != (u32tmp & 0x3ffffff)) {	/*  */
		tlsc_err("tlsc6x:ptcfg version error,g_tlsc6x_cfg_ver is 0x%x:ptcfg version is 0x%x!\n",
		g_tlsc6x_cfg_ver, u32tmp);
		return 0;
	}

	if (g_tlsc6x_cfg_ver == u32tmp) {
		tlsc_err("Tlsc6x:cfg_ver is same %d\n", g_tlsc6x_cfg_ver);
		return 0;
	}

	return 1;
}

/* 0 error */
/* 0x7f80 no data */
static u16 tlsx6x_find_last_cfg(void)
{
	unsigned short addr, check;

	addr = 0x7f80 - 256;
	while (addr > 0x6000) {	/* 0x6080 */
		check = 0;
		if (tlsc6x_read_burn_space((u8 *) &check, addr, 2)) {
			addr = 0;
			goto exit;
		}
		if (check == 0xffff) {
			break;
		}
		addr -= 256;
	}

	addr += 256;

exit:

	return addr;
}

#ifdef TLSC_AUTO_UPGRADE
static int find_3535last_valid_burn_cfg(u16 *ptcfg)
{
	unsigned short addr;

	if (tlsc6x_download_ramcode(nvm_burn_bin_3535, sizeof(nvm_burn_bin_3535))) {
		return -EPERM;
	}

	addr = tlsx6x_find_last_cfg();
	if ((addr == 0) || (addr == 0x7f80)) {
		return -EPERM;
	}

	while (addr <= 0x7e80) {
		if (tlsc6x_read_burn_space((u8 *) ptcfg, addr, 204)) {
			addr = 0x7f80;	/* force error */
			break;
		}
		if (is_valid_cfg_data(ptcfg) == 0) {
			addr += 256;
		} else {
			break;
		}
	}

	return (addr <= 0x7e80) ? 0 : -1;

}
#endif
/* NOT a public function, only one caller!!! */
void tlsc6x_tp_cfg_version(void)
{
	unsigned int tmp;

	if (tlsc6x_set_dd_mode()) {
		return;
	}
	if (g_mccode == 1) {
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x9e00, (u8 *) &tmp, 4)) {
			return;
		}
		g_tlsc6x_cfg_ver = tmp;

		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x9e00 + (53 * 2), (u8 *) &tmp, 2)) {
			return;
		}
		g_tlsc6x_chip_code = (unsigned short)tmp;
	} else {
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0xd6e0, (u8 *) &tmp, 4)) {
			return;
		}

		g_tlsc6x_cfg_ver = tmp;

		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0xd6e0 + (53 * 2), (u8 *) &tmp, 2)) {
			return;
		}
		g_tlsc6x_chip_code = (unsigned short)tmp;
	}

	if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x8008, (u8 *) &tmp, 4) == 0) {
		if (tmp == 0x544c4e4b) {
			if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x8004, (u8 *) &tmp, 2)) {
				return;
			}
			g_tlsc6x_boot_ver = tmp & 0xffff;
		} else {
			tlsc_info("%s check chip work failed 0x%x\n", __func__, tmp);
		}
	}

	tlsc_info("%s, cfg_ver is 0x%x,chip_code is 0x%x,code_type=%d,boot_ver=%d\n",
		__func__, g_tlsc6x_cfg_ver, g_tlsc6x_chip_code, g_needKeepRamCode,
		g_tlsc6x_boot_ver);
}


/* NOT a public function, only one caller!!! */
static void tlsc6x_tp_mccode(void)
{
	unsigned int tmp[3];

	g_mccode = 0xff;

	if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x8000, (u8 *) tmp, 12)) {
		return;
	}
	if (tmp[2] == 0x544c4e4b) {	/*  boot code  */
		if (tmp[0] == 0x35368008) {
			g_mccode = 1;
		} else if (tmp[0] == 0x35358008) {
			g_mccode = 0;
		}
	} else {	/* none code */
		tmp[0] = 0;
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x09, (u8 *) tmp, 3)) {
			return;
		}
		if ((tmp[0] == 0x444240) || (tmp[0] == 0x5c5c5c)) {
			g_mccode = 1;
		} else {
			g_mccode = 0;
		}
	}

	if (g_mccode == 0) {
		MTK_TXRX_BUF = 0x80cc00;
		CMD_ADDR = 0x80b400;
		RSP_ADDR = 0x80b440;
	} else {
		MTK_TXRX_BUF = 0x809000;
		CMD_ADDR = 0x809f00;
		RSP_ADDR = 0x809f40;
	}

}

int tlsx6x_update_3536_cfg(u16 *ptcfg)
{
	if (tlsc6x_tpcfg_ver_comp_weak(ptcfg) == 0) {
		tlsc_err("Tlsc6x:update error:version error!\n");
		return -EPERM;
	}

	if (g_tlsc6x_cfg_ver && ((u16) (g_tlsc6x_cfg_ver & 0xffff) != ptcfg[0])) {
		return -EPERM;
	}

	if (tlsc6x_download_ramcode(nvm_burn_bin_3536, sizeof(nvm_burn_bin_3536))) {
		tlsc_err("Tlsc6x:update error:ram-code error!\n");
		return -EPERM;
	}

	if (tlsc6x_write_burn_space((unsigned char *)ptcfg, 0x8000, 204)) {
		tlsc_err("Tlsc6x:update fcomp_cfg fail!\n");
		return -EPERM;
	}

	tlsc_info("Tlsc6x:update fcomp_cfg pass!\n");

	memcpy(tl_target_cfg, ptcfg, 204);
	g_tlsc6x_cfg_ver = (ptcfg[1] << 16) | ptcfg[0];

	return 0;
}

/* used for 3536, not suitable to 3535  */
int tlsx6x_update_fcomp_boot(unsigned char *pdata, u16 len)
{
	TLSC_FUNC_ENTER();

	if (tlsc6x_download_ramcode(nvm_burn_bin_3536, sizeof(nvm_burn_bin_3536))) {
		tlsc_err("Tlsc6x:update error:ram-code error!\n");
		return -EPERM;
	}
	pdata[8] = 0xff;
	if (tlsc6x_write_burn_space((unsigned char *)pdata, 0x00, len)) {
		tlsc_err("Tlsc6x:update fcomp_boot fail!\n");
		return -EPERM;
	}
	pdata[8] = 0x4b;
	if (tlsc6x_write_burn_space(&pdata[8], 0x08, 1)) {
		tlsc_err("Tlsc6x:update fcomp_boot last sig-byte fail!\n");
		return -EPERM;
	}
	g_tlsc6x_boot_ver = pdata[5];
	g_tlsc6x_boot_ver = (g_tlsc6x_boot_ver<<8) + pdata[4];
	tlsc_info("Tlsc6x:update fcomp_boot pass!\n");

	return 0;
}
/*
int tlsx6x_update_ocomp_boot(unsigned char *pdata, u16 len)
{
	unsigned int oo_tail[4] = { 0x60298bf, 0x15cbf, 0x60301bf, 0x3d43f };

	if (tlsc6x_download_ramcode(nvm_burn_bin_3535, sizeof(nvm_burn_bin_3535))) {
		tlsc_err("Tlsc6x:update error:ram-code error!\n");
		return -EPERM;
	}

	if (tlsc6x_write_burn_space((unsigned char *)pdata, 0x00, len)) {
		tlsc_err("Tlsc6x:update ocomp_boot fail!\n");
		return -EPERM;
	}

	if (tlsc6x_write_burn_space((unsigned char *)oo_tail, 0x7fec, 16)) {
		tlsc_err("Tlsc6x:update ocomp_boot fail!\n");
		return -EPERM;
	}
	tlsc_info("Tlsc6x:update ocomp_boot pass!\n");

	return 0;
}
*/
/* update composite package by force(adb path) */
static int tlsc6x_update_cfgarray_force(unsigned short *parray, unsigned int cfg_num)
{
	int idx_active;
	unsigned int  k;

	TLSC_FUNC_ENTER();

	tlsc_info("g_tlsc6x_cfg_ver is 0x%x\n", g_tlsc6x_cfg_ver);
	if (g_tlsc6x_cfg_ver == 0) {	/* no available version information */
		tlsc_err("Tlsc6x:no current version information!\n");
		return 0;
	}

	idx_active = -1;

	for (k = 0; k < cfg_num; k++) {
		if (tlsc6x_tpcfg_ver_comp_weak(parray) == 1) {
			idx_active = k;
			tlsc_info("%s, new_idx_active is %d.\n", __func__, idx_active);
			break;
		}
		parray = parray + 102;
	}

	if (idx_active < 0) {
		tlsc_info("Tlsc6x:auto update skip:no updated version!\n");
		return -EPERM;
	}

	if (tlsc6x_set_dd_mode()) {
		tlsc_err("Tlsc6x:update error:can't control hw mode!\n");
		return -EPERM;
	}

	if (tlsx6x_update_burn_cfg(parray) == 0) {
		tlsc_info("Tlsc6x:cfg update pass!\n");
	} else {
		tlsc_err("Tlsc6x:cfg update fail!\n");
	}

	return 1;		/* need hw reset */

}

/* update composite package by force(adb path) */
int tlsc6x_update_comppkg_force(u8 *pupd, u16 len)
{
	u32 k;
	u32 n;
	u32 offset;
	u32 *vlist;

	struct tlsc6x_updfile_header *upd_header;

	if (len < sizeof(struct tlsc6x_updfile_header)) {
		return -EPERM;
	}

	upd_header = (struct tlsc6x_updfile_header *) pupd;

	if (upd_header->sig != 0x43534843) {
		return -EPERM;
	}

	n = upd_header->n_cfg;
	offset = (upd_header->n_match * 4) + sizeof(struct tlsc6x_updfile_header);

	if ((offset + upd_header->len_cfg + upd_header->len_boot) != len) {
		return -EPERM;
	}

	if ((n * 204) != upd_header->len_cfg) {
		return -EPERM;
	}

	if (n != 0) {
		tlsc6x_update_cfgarray_force((u16 *) (pupd + offset), n);
	}

	n = upd_header->n_match;
	if ((n != 0) && (g_mccode != 0)) {/* 3535 not support */
		vlist = (u32 *) (pupd + sizeof(struct tlsc6x_updfile_header));
		offset = offset + upd_header->len_cfg;
		for (k = 0; k < n; k++) {
			if (vlist[k] == (g_tlsc6x_cfg_ver & 0xffffff)) {
				tlsx6x_update_fcomp_boot((pupd + offset), upd_header->len_boot);
				break;
			}
		}
	}
	return 0;
}

int tlsc6x_update_f_combboot(u8 *pdata, u16 len)
{
	int ret = 0;
	unsigned short boot_len;
	struct tlsc6x_updfile_header *upd_header;

	if (len < sizeof(struct tlsc6x_updfile_header)) {
		return -EPERM;
	}

	upd_header = (struct tlsc6x_updfile_header *) pdata;

	if (upd_header->sig == 0x43534843) {
		return tlsc6x_update_comppkg_force(pdata, len);
	}

	boot_len = pdata[7];
	boot_len = (boot_len << 8) + pdata[6];

	if (g_mccode == 0) {
		/* close this interface for 3535 */
		return -EPERM;
	}

	if ((len >= boot_len) && ((pdata[2] == 0x36) && (pdata[3] == 0x35))) {
		ret = tlsx6x_update_fcomp_boot(pdata, boot_len);
		pdata += boot_len;
		len = len - boot_len;
	}

	if ((ret == 0) && (len >= 204)) {
		memcpy(tl_buf_tmpcfg, pdata, 204);
		ret = tlsx6x_update_3536_cfg(tl_buf_tmpcfg);
	}

	return ret;

}

int tlsx6x_update_burn_cfg(u16 *ptcfg)
{
	u16 addr, check;

	TLSC_FUNC_ENTER();
	if (g_mccode == 1) {
		return tlsx6x_update_3536_cfg(ptcfg);
	}

	if (tlsc6x_tpcfg_ver_comp_weak(ptcfg) == 0) {
		tlsc_err("Tlsc6x:update error:version error!\n");
		return -EPERM;
	}

	if (g_tlsc6x_cfg_ver && ((u16) (g_tlsc6x_cfg_ver & 0xffff) != ptcfg[0])) {
		return -EPERM;
	}

	if (tlsc6x_download_ramcode(nvm_burn_bin_3535, sizeof(nvm_burn_bin_3535))) {
		tlsc_err("Tlsc6x:update error:ram-code error!\n");
		return -EPERM;
	}

	addr = tlsx6x_find_last_cfg();
	if ((addr <= 0x6180)) { /*   || (addr == 0x7f80) */
		tlsc_err("Tlsc6x:update error:time limit!\n");
		return -EPERM;
	}

	addr = addr - 256;

	/* pre-check */
	check = 0;
	if (tlsc6x_read_burn_space((unsigned char *)&check, addr - 256, 2)) {
		tlsc_err("Tlsc6x:update error:pre-read error!\n");
		return -EPERM;
	}
	if (check != 0xffff) {
		tlsc_err("Tlsc6x:update error:pre-read limit!\n");
		return -EPERM;
	}

	if (tlsc6x_write_burn_space((unsigned char *)ptcfg, addr, 204)) {
		tlsc_err("Tlsc6x:update fail!\n");
		return -EPERM;
	}

	tlsc_info("Tlsc6x:update pass!\n");

	memcpy(tl_target_cfg, ptcfg, 204);
	g_tlsc6x_cfg_ver = (ptcfg[1] << 16) | ptcfg[0];

	return 0;
}

/* NOTE:caller guarantee the legitimacy */
/* download tp-cfg. */
int tlsx6x_update_running_cfg(u16 *ptcfg)
{
	unsigned int retry;
	unsigned int tmp[2];

	TLSC_FUNC_ENTER();
	if (is_valid_cfg_data(ptcfg) == 0) {
		return -EPERM;
	}
	if (tlsc6x_set_dd_mode()) {
		return -EPERM;
	}

	if (tlsc6x_bulk_down_check((unsigned char *)ptcfg, 0xd7e0, 204)) {	/* stop mcu */
		goto exit;
	}

	tmp[0] = 0x6798;
	tmp[1] = 0xcd3500ff;

	retry = 0;
	while (++retry < 3) {
		if (tlsc6x_write_bytes_u16addr(g_tlsc6x_client, 0xdf10, (u8 *) &tmp[0], 8)) {
			usleep_range(5000, 5500);
			continue;
		}
		break;
	}

	/* write error? don't care */
	retry = 0;
	while (++retry < 5) {
		usleep_range(10000, 11000);
		tmp[0] = 0;
		tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0xdf16, (u8 *) &tmp[0], 1);
		if (tmp[0] == 0x30) {
			break;
		}
	}

exit:
	tlsc6x_set_nor_mode();
	memcpy(tl_target_cfg, ptcfg, 204);
	return 0;
}

/*
*get running time tp-cfg.
*@ptcfg:	data buffer
*@addr:	real data address for different chips
*
*return: 0 SUCCESS else FAIL
*Note: touch chip MUST work in DD-mode.
*/
static int tlsx6x_comb_get_running_cfg(u16 *ptcfg, u16 addr)
{
	int retry, err_type;

	TLSC_FUNC_ENTER();
	retry = 0;
	err_type = 0;

	tlsc6x_set_dd_mode();

	while (++retry < 5) {
		err_type = 0;
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, addr, (u8 *) ptcfg, 204)) {
			msleep(20);
			err_type = 2;	/* i2c error */
			continue;
		}

		if (is_valid_cfg_data(ptcfg) == 0) {
			tlsc6x_set_dd_mode();
			err_type = 1;	/* data error or no data */
			msleep(20);
			continue;
		}
		break;
	}

	return err_type;

}


static int tlsx6x_3535get_running_cfg(unsigned short *ptcfg)
{

	TLSC_FUNC_ENTER();

	return tlsx6x_comb_get_running_cfg(ptcfg, 0xd6e0);
}

static int tlsx6x_3536get_running_cfg(unsigned short *ptcfg)
{

	TLSC_FUNC_ENTER();

	return tlsx6x_comb_get_running_cfg(ptcfg, 0x9e00);
}

int tlsc6x_get_running_cfg(unsigned short *ptcfg)
{
	if (g_mccode == 0) {
		return tlsx6x_3535get_running_cfg(ptcfg);
	}
	return tlsx6x_3536get_running_cfg(ptcfg);
}

static int tlsx6x_3535find_lastvaild_ver(void)
{
	if (find_3535last_valid_burn_cfg(tl_buf_tmpcfg) == 0) {
		g_tlsc6x_cfg_ver = (unsigned int)tl_buf_tmpcfg[1];
		g_tlsc6x_cfg_ver = (g_tlsc6x_cfg_ver<<16) + (unsigned int)tl_buf_tmpcfg[0];
		g_tlsc6x_cfg_ver = g_tlsc6x_cfg_ver&0x3ffffff;/* force set sub-version to zero */
	}

	return 0;
}

static int tlsx6x_3536find_lastvaild_ver(void)
{
	if (tlsc6x_download_ramcode(nvm_burn_bin_3536, sizeof(nvm_burn_bin_3536))) {
		tlsc_err("Tlsc6x:update error:ram-code error!\n");
		return -EPERM;
	}

	if (tlsc6x_read_burn_space((u8 *) tl_buf_tmpcfg, 0x8000, 204)) {
		return -EPERM;
	}


	if (is_valid_cfg_data(tl_buf_tmpcfg) == 0) {
		if (tlsc6x_read_burn_space((u8 *) tl_buf_tmpcfg, 0xf000, 204)) {
			return -EPERM;
		}
	}

	if (is_valid_cfg_data(tl_buf_tmpcfg)) {
		g_tlsc6x_cfg_ver = (unsigned int)tl_buf_tmpcfg[1];
		g_tlsc6x_cfg_ver = (g_tlsc6x_cfg_ver<<16) + (unsigned int)tl_buf_tmpcfg[0];
		g_tlsc6x_cfg_ver = g_tlsc6x_cfg_ver&0x3ffffff;/* force set sub-version to zero */
	}
	return 0;
}

/* try to find out the most accurate version information(pid & vid) */
int tlsc6x_find_ver(void)
{
	if (g_mccode == 0) {
		return tlsx6x_3535find_lastvaild_ver();
	}

	return tlsx6x_3536find_lastvaild_ver();
}

#ifdef TLSC_AUTO_UPGRADE
static int tlsc6x_upgrade_romcfg_array(unsigned short *parray, unsigned int cfg_num)
{
	unsigned int  k;

	TLSC_FUNC_ENTER();

	tlsc_info("g_tlsc6x_cfg_ver is 0x%x\n", g_tlsc6x_cfg_ver);
	if (g_tlsc6x_cfg_ver == 0) {	/* no available version information */
		tlsc_err("Tlsc6x:no current version information!\n");
		return 0;
	}

	new_idx_active = -1;

	for (k = 0; k < cfg_num; k++) {
		if (tlsc6x_tpcfg_ver_comp(parray) == 1) {
			new_idx_active = k;
			tlsc_info("%s, new_idx_active is %d.\n", __func__, new_idx_active);
			break;
		}
		parray = parray + 102;
	}

	if (new_idx_active < 0) {
		tlsc_info("Tlsc6x:auto update skip:no updated version!\n");
		return -EPERM;
	}

	if (tlsc6x_set_dd_mode()) {
		tlsc_err("Tlsc6x:auto update error:can't control hw mode!\n");
		return -EPERM;
	}

	if (tlsx6x_update_burn_cfg(parray) == 0) {
		tlsc_info("Tlsc6x:update pass!\n");
	} else {
		tlsc_err("Tlsc6x:update fail!\n");
	}

	return 1;		/* need hw reset */

}


static int tlsc6x_boot_ver_comp(unsigned int ver)
{
	if ((g_tlsc6x_boot_ver == 0) || (ver  > g_tlsc6x_boot_ver)) {
		return 1;
	}
	return 0;
}

static int tlsc6x_3536boot_update(u8 *pdata, u16 boot_len)
{
	unsigned int ver = 0;

	ver = pdata[5];
	ver = (ver<<8) + pdata[4];

	if (tlsc6x_boot_ver_comp(ver) == 0) {
		tlsc_info("Tlsc6x:3536 boot not need update!\n");
		return 0;
	}

	return  tlsx6x_update_fcomp_boot(pdata, boot_len);
}

static int tlsc6x_3535boot_update(u8 *pdata, u16 boot_len)
{
	unsigned int ver = 0;

	ver = pdata[5];
	ver = (ver<<8) + pdata[4];

	if (tlsc6x_boot_ver_comp(ver) == 0) {
		tlsc_info("Tlsc6x:3535 boot not need update!\n");
		return 0;
	}

	g_needKeepRamCode = 1;

	return tlsc6x_load_ext_binlib(pdata, boot_len);
}

static int tlsc6x_boot_update(u8 *pdata, u16 boot_len)
{

	if (g_mccode == 0) {
		return tlsc6x_3535boot_update(pdata, boot_len);
	}

	return tlsc6x_3536boot_update(pdata, boot_len);
}


int tlsc6x_update_compat_ctl(u8 *pupd, int len)
{
	u32 k;
	u32 n;
	u32 offset;
	u32 *vlist;

	struct tlsc6x_updfile_header *upd_header;

	if (len < sizeof(struct tlsc6x_updfile_header)) {
		return -EPERM;
	}

	upd_header = (struct tlsc6x_updfile_header *) pupd;

	if (upd_header->sig != 0x43534843) {
		return -EPERM;
	}

	n = upd_header->n_cfg;
	offset = (upd_header->n_match * 4) + sizeof(struct tlsc6x_updfile_header);

	if ((offset + upd_header->len_cfg + upd_header->len_boot) != len) {
		return -EPERM;
	}

	if ((n * 204) != upd_header->len_cfg) {
		return -EPERM;
	}

	if (n != 0) {
		tlsc6x_upgrade_romcfg_array((u16 *) (pupd + offset), n);
	}

	n = upd_header->n_match;
	if (n != 0) {
		vlist = (u32 *) (pupd + sizeof(struct tlsc6x_updfile_header));
		offset = offset + upd_header->len_cfg;
		for (k = 0; k < n; k++) {
			if (vlist[k] == (g_tlsc6x_cfg_ver & 0xffffff)) {
				tlsc6x_boot_update((pupd + offset), upd_header->len_boot);

				break;
			}
		}
	}
	return 0;
}
#endif

void tlsc6x_do_update_ifneed(void)
{
#ifdef TLSC_AUTO_UPGRADE
	int ret;
	u8 *pupd;
	const struct firmware *fw = NULL;
	char fwname[50] = { 0 };

	snprintf(fwname, sizeof(fwname), "%s%s.bin", CHSC_AUTO_UPD_FNAME, tlsc6x_vendor_name);
	ret = request_firmware(&fw, fwname, &g_tlsc6x_client->dev);
	if (ret) {
		tlsc_err("Update fail, unable to open firmware %s\n", fwname);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_REQUEST_FIRMWARE_ERROR_NO);
#endif
		return;
	}
	tlsc_irq_disable();
	g_tp_drvdata->esdHelperFreeze = 1;
	if (fw->size > (64*1024)) {
		ret = -EIO;
		tlsc_err("fw->size is %d, too big file(>64KB)\n", fw->size);
		goto exit;
	}

	/* alloc successive memory */
	pupd = kzalloc(fw->size, GFP_KERNEL);
	if (!pupd) {
		ret = -ENOMEM;
		tlsc_err("Update fail, kzalloc %d bytes fail\n", fw->size);
		goto exit;
	}

	memcpy(pupd, fw->data, fw->size);

	ret = tlsc6x_update_compat_ctl(pupd, (int)fw->size);
	tlsc6x_tpd_reset_force();
	kfree(pupd);

exit:
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (ret < 0)
		tpd_zlog_record_notify(TP_FW_UPGRADE_ERROR_NO);
#endif
	g_tp_drvdata->esdHelperFreeze = 0;
	tlsc_irq_enable();
	release_firmware(fw);
	return;
#else
	tlsc_err("Tlsc6x:auto update error:no build-in file!\n");
	return;		/* no hw resetting needed */
#endif

}

#ifdef TLSC_GESTRUE
/* 0:successful */
static int tlsc6x_download_gestlib_fast(u8 *pcode, u16 len)
{
	u8 dwr, retry;
	int ret = -2;

	TLSC_FUNC_ENTER();
	if (tlsc6x_set_dd_mode()) {
		return -EPERM;
	}

	dwr = 0x05;
	if (tlsc6x_bulk_down_check(&dwr, 0x0602, 1) == 0) {	/* stop mcu */
		dwr = 0x00;
		tlsc6x_bulk_down_check(&dwr, 0x0643, 1);	/* disable irq */
	} else {
		return -EPERM;
	}

	if (tlsc6x_bulk_down_check(pcode, 0x8000, 1024) == 0) {
		if (tlsc6x_write_bytes_u16addr(g_tlsc6x_client, 0x8400, pcode + 1024, len - 1024) == 0) {	/*  */
			ret = 0;
		}
	}

	if (ret == 0) {
		dwr = 0x88;
		retry = 0;
		do {
			ret = tlsc6x_write_bytes_u16addr(g_tlsc6x_client, 0x0602, &dwr, 1);
		} while ((++retry < 3) && (ret != 0));
	}

	msleep(40);		/* 30ms */
	if (tlsc6x_get_i2cmode() == DIRECTLY_MODE) {
		ret = tlsc6x_download_ramcode(pcode, len);
	}

	return ret;
}

int tlsc6x_load_gesture_binlib(void)
{
	int ret;

	TLSC_FUNC_ENTER();
	ret = tlsc6x_download_gestlib_fast(tlsc6x_gesture_binlib, sizeof(tlsc6x_gesture_binlib));
	if (ret) {
		tlsc_err("Tlsc6x:load gesture binlib error!\n");
		return -EPERM;
	}
	return 0;
}
#endif

#ifdef TLSC_BUILDIN_BOOT
int tlsc6x_auto_down_3535prj(void)
{
	TLSC_FUNC_ENTER();
	if (tlsc6x_get_i2cmode() == DEDICATE_MODE) {
		return -EPERM;
	}
	/* calibrate if needed */

	tlsc6x_download_ramcode(tlsc6x_boot_bin, sizeof(tlsc6x_boot_bin));

	msleep(30);

	tlsx6x_update_running_cfg(&buidIn_tls_tp_lut[0][0]);

	tlsc6x_set_nor_mode();

	g_tlsc6x_boot_ver = tlsc6x_boot_bin[5];
	g_tlsc6x_boot_ver = (g_tlsc6x_boot_ver << 8) + tlsc6x_boot_bin[4];

	return 0;
}
#endif

int tlsc6x_load_ext_binlib(u8 *pdata, u16 len)
{
	int ret = 0;
	unsigned short boot_len;

	TLSC_FUNC_ENTER();
	if (g_mccode == 1) {
		return 0;
	}

	boot_len = pdata[7];
	boot_len = (boot_len << 8) + pdata[6];

	if ((len >= boot_len) && ((pdata[2] == 0x35) && (pdata[3] == 0x35))) {
		ret = tlsc6x_download_ramcode(pdata, boot_len);
		pdata += boot_len;
		len = len - boot_len;
		if ((ret == 0) && (len >= 204)) {
			msleep(30);
			ret = tlsx6x_update_running_cfg((u16 *) pdata);
		}
	}

	return 0;
}

#ifdef TLSC_TP_PROC_SELF_TEST

struct tlsc6x_stest_crtra *pt_crtra_data = NULL;
unsigned short m_os_rmap_buf[48];
unsigned short m_os_test_buf[48];
char *g_tlsc_crtra_file = NULL;
unsigned short g_allch_num = 0;
int tlsc6x_selftest_crtra(void)
{
	int ret, k, idx;
	const struct firmware *fw = NULL;

	TLSC_FUNC_ENTER();
	if (g_tlsc_crtra_file == NULL) {
		return -EIO;
	}

	tlsc_info("open firmware %s\n", g_tlsc_crtra_file);

	ret = request_firmware(&fw, g_tlsc_crtra_file, &g_tlsc6x_client->dev);
	if (ret) {
		tlsc_err("Unable to open firmware %s\n", g_tlsc_crtra_file);
		return -EIO;
	}
	if (fw->size != sizeof(*pt_crtra_data)) {
		ret = -EIO;
		tlsc_err("fw->size is %d,fw->size error. sizeof(*pt_crtra_data)is %d\n",
			fw->size, sizeof(*pt_crtra_data));
		goto exit;
	}

	pt_crtra_data = kzalloc(sizeof(*pt_crtra_data), GFP_KERNEL);
	if (!pt_crtra_data) {
		ret = -ENOMEM;
		goto exit;
	}

	memcpy(pt_crtra_data, fw->data, fw->size);

	ret = 0;
	g_allch_num = (unsigned short)pt_crtra_data->allch_n;
	tlsc_info("g_allch_num: %d\n", g_allch_num);
	for (k = 0; k < 48; k++) { /* tlsc tp max channel is 48 */
		idx = pt_crtra_data->remap[k];
		if (idx) {
			m_os_rmap_buf[idx - 1] = m_os_test_buf[k];
			if (m_os_test_buf[k] > pt_crtra_data->rawmax[k]) {
				ret |= 1;
			}
			if (m_os_test_buf[k] < pt_crtra_data->rawmin[k]) {
				ret |= 2;
			}
		}
	}
exit:
	if (pt_crtra_data != NULL) {
		kfree(pt_crtra_data);
		pt_crtra_data = NULL;
	}
	release_firmware(fw);

	return ret;
}

int tlsc6x_rawdata_test_record(void)
{
	int ret;
	int loop = 0;
	unsigned int flen = 0;
	unsigned char *pstr_buf = NULL;

	ret = tlsc6x_selftest_crtra();

	pstr_buf = kzalloc(MAX_BULK_SIZE, GFP_KERNEL);	/* auto clear */
	if (pstr_buf == NULL) {
		tlsc_err("Tlsc6x:tlsc6x_rawdata_test_allch error::alloc file buffer fail!\n");
		return ret;
	}
	flen = 0;
	for (loop = 0; loop < 47; loop++) {
		if (loop && ((loop % 0x7) == 0)) {
			flen += snprintf(pstr_buf + flen, MAX_BULK_SIZE - flen, "%05d,\n", m_os_test_buf[loop]);
		} else {
			flen += snprintf(pstr_buf + flen, MAX_BULK_SIZE - flen, "%05d,", m_os_test_buf[loop]);
		}
	}
	flen += snprintf(pstr_buf + flen, MAX_BULK_SIZE - flen, "%05d\n", m_os_test_buf[loop]);
	if (ret == 0) {
		tlsc6x_fif_write(NULL, "tlsc tp self test success!\n", 0);
	} else {
		tlsc6x_fif_write(NULL, "tlsc tp self test fail!\n", 0);
	}
	tlsc6x_fif_write(NULL, pstr_buf, 0);
	kfree(pstr_buf);
	return ret;
}


int tlsc6x_rawdata_test_3535allch(void)
{
	int ret;
	unsigned short sync;


	TLSC_FUNC_ENTER();
	tlsc6x_download_ramcode(rawdata_test_bin_3535, sizeof(rawdata_test_bin_3535));
	msleep(100);

	ret = 0;
	sync = 0;
	while (1) {
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0xc330, (u8 *) &sync, 2) == 0) {
			if (sync == 0x6306) {
				ret = 0;
				break;
			}
		}
		if (++ret > 20) {
			ret = -2;
			break;
		}
		msleep(50);
	}
	if (ret < 0) {
		tlsc_err("Tlsc6x:tlsc6x_rawdata_test_allch error::timeout!\n");
		goto exit;
	}

	if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0xc000, (u8 *) m_os_test_buf, 72)) {
		tlsc_err("Tlsc6x:tlsc6x_rawdata_test_allch error::read rawdata fail!\n");
		goto exit;
	}

	ret = tlsc6x_rawdata_test_record();

exit:
	tlsc6x_tpd_reset_force();
#ifdef TLSC_BUILDIN_BOOT
	if (g_needKeepRamCode == 1) {
		tlsc6x_auto_down_3535prj();
	}
#endif

	return ret;
}

int tlsc6x_rawdata_test_3536allch(void)
{
	int ret;
	unsigned short sync;

	TLSC_FUNC_ENTER();
	tlsc6x_download_ramcode(rawdata_test_bin_3536, sizeof(rawdata_test_bin_3536));
	msleep(100);

	ret = 0;
	sync = 0;
	while (1) {
		if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x9f10, (u8 *) &sync, 2) == 0) {
			if (sync == 0x6306) {
				ret = 0;
				break;
			}
		}
		if (++ret > 20) {
			ret = -2;
			break;
		}
		msleep(50);
	}
	if (ret < 0) {
		tlsc_err("Tlsc6x:tlsc6x_rawdata_test_allch error::timeout!\n");
		goto exit;
	}

	if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x9d10, (u8 *) m_os_test_buf, 96)) {
		tlsc_err("Tlsc6x:tlsc6x_rawdata_test_allch error::read rawdata fail!\n");
		goto exit;
	}

	ret = tlsc6x_rawdata_test_record();

exit:
	tlsc6x_tpd_reset_force();
#ifdef TLSC_BUILDIN_BOOT
	if (g_needKeepRamCode == 1) {
		tlsc6x_auto_down_3535prj();
	}
#endif

	return ret;
}

int tlsc6x_chip_self_test_sub(void)
{
	TLSC_FUNC_ENTER();

	if (g_mccode == 0) {
		return tlsc6x_rawdata_test_3535allch();
	} else {
		return tlsc6x_rawdata_test_3536allch();
	}
}
#endif

#ifdef TLSC_BUILDIN_BOOT
int tlsc6x_tp_fw_ready(void)
{
	unsigned int tmp;

	TLSC_FUNC_ENTER();
	if (tlsc6x_set_dd_mode()) {
		return 0;
	}

	if (tlsc6x_read_bytes_u16addr(g_tlsc6x_client, 0x8008, (u8 *) &tmp, 4)) {
		return 0;
	}

	if (tmp == 0xffffffff) {
		return 1;
	}

	return 0;
}
#endif

/* is tlsc6x module ? */
int tlsc6x_tp_dect(struct i2c_client *client)
{
	TLSC_FUNC_ENTER();

	g_mccode = 0;		/* default */
	g_tlsc6x_client = client;

	if (tlsc6x_set_dd_mode()) {
		return 0;
	}

	tlsc6x_tp_mccode();	/* MUST: call this function there!!! */
	tlsc_info("g_mccode is %d\n", g_mccode);
#ifdef TLSC_BUILDIN_BOOT
	if (g_mccode == 0) {
		g_needKeepRamCode = (unsigned int)tlsc6x_tp_fw_ready();
		if (g_needKeepRamCode == 1) {
			tlsc6x_auto_down_3535prj();
		}
	}
#endif

	tlsc6x_tp_cfg_version();	/* MUST: call this function there!!! */

	/*try to get running time tp-cfg. if fail : wrong boot? wrong rom-cfg?*/
	if (tlsc6x_get_running_cfg(tl_buf_tmpcfg) == 0) {
		g_tlsc6x_cfg_ver = (unsigned int)tl_buf_tmpcfg[1];
		g_tlsc6x_cfg_ver = (g_tlsc6x_cfg_ver<<16) + (unsigned int)tl_buf_tmpcfg[0];
		g_tlsc6x_chip_code = (unsigned short)tl_buf_tmpcfg[53];
	} else {
		tlsc6x_find_ver();
	}

	tlsc6x_set_nor_mode();

	return 1;
}
