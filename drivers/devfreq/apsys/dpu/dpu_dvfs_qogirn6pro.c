// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/soc/sprd/hwfeature.h>

#include "sprd_dvfs_apsys.h"
#include "sprd_dvfs_dpu.h"
#include "../sys/apsys_dvfs_qogirn6pro.h"

enum {
	V55 = 0, //0.55v
	V60 = 1, //0.60v
	V65 = 2, //0.65v
	V70 = 3, //0.70v
	V75 = 4, //0.75v
};

enum {
	CLK_INDEX_256M = 0,
	CLK_INDEX_307M2,
	CLK_INDEX_384M,
	CLK_INDEX_409M6,
	CLK_INDEX_512M,
	CLK_INDEX_614M4,
};

static struct ip_dvfs_map_cfg map_table[] = {
	{0, V65, CLK_INDEX_256M, DPU_CLK256M},
	{1, V65, CLK_INDEX_307M2, DPU_CLK307M2},
	{2, V65, CLK_INDEX_384M, DPU_CLK384M},
	{3, V70, CLK_INDEX_409M6, DPU_CLK409M6},
	{4, V70, CLK_INDEX_512M,  DPU_CLK512M},
	{5, V75, CLK_INDEX_614M4, DPU_CLK614M4},
	{6, V75, CLK_INDEX_614M4, DPU_CLK614M4},
	{7, V75, CLK_INDEX_614M4, DPU_CLK614M4},
};

static void dpu_hw_dfs_en(struct dpu_dvfs *dpu, bool dfs_en)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	if (dfs_en)
		reg->dpu_vsp_dfs_en_ctrl |= BIT(0);
	else
		reg->dpu_vsp_dfs_en_ctrl &= ~BIT(0);
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void dpu_dvfs_map_cfg(struct dpu_dvfs *dpu)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	reg->dispc_index0_map = map_table[0].clk_level |
		map_table[0].volt_level << 3;
	reg->dispc_index1_map = map_table[1].clk_level |
		map_table[1].volt_level << 3;
	reg->dispc_index2_map = map_table[2].clk_level |
		map_table[2].volt_level << 3;
	reg->dispc_index3_map = map_table[3].clk_level |
		map_table[3].volt_level << 3;
	reg->dispc_index4_map = map_table[4].clk_level |
		map_table[4].volt_level << 3;
	reg->dispc_index5_map = map_table[5].clk_level |
		map_table[5].volt_level << 3;
	reg->dispc_index6_map = map_table[5].clk_level |
		map_table[5].volt_level << 3;
	reg->dispc_index7_map = map_table[5].clk_level |
		map_table[5].volt_level << 3;
}

static void set_dpu_work_freq(struct dpu_dvfs *dpu, u32 freq)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		if (map_table[i].clk_rate == freq) {
			reg->dispc_dvfs_index_cfg = i;
			break;
		}
	}
}

static u32 get_dpu_work_freq(struct dpu_dvfs *dpu)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;
	u32 freq = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		if (map_table[i].map_index ==
			reg->dispc_dvfs_index_cfg) {
			freq = map_table[i].clk_rate;
			break;
		}
	}

	return freq;
}

static void set_dpu_idle_freq(struct dpu_dvfs *dpu, u32 freq)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		if (map_table[i].clk_rate == freq) {
			reg->dispc_dvfs_index_idle_cfg = i;
			break;
		}
	}
}

static u32 get_dpu_idle_freq(struct dpu_dvfs *dpu)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;
	u32 freq = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		if (map_table[i].map_index ==
			reg->dispc_dvfs_index_idle_cfg) {
			freq = map_table[i].clk_rate;
			break;
		}
	}

	return freq;
}

static void set_dpu_work_index(struct dpu_dvfs *dpu, int index)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	reg->dispc_dvfs_index_cfg = index;
}

static int get_dpu_work_index(struct dpu_dvfs *dpu)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	return reg->dispc_dvfs_index_cfg;
}

static void set_dpu_idle_index(struct dpu_dvfs *dpu, int index)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	reg->dispc_dvfs_index_idle_cfg = index;
}

static int get_dpu_idle_index(struct dpu_dvfs *dpu)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	return reg->dispc_dvfs_index_idle_cfg;
}

static void set_dpu_gfree_wait_delay(struct dpu_dvfs *dpu, u32 para)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;
	u32 temp;

	mutex_lock(&dpu->apsys->reg_lock);
	temp = reg->dpu_vsp_gfree_wait_delay_cfg0;
	temp &= GENMASK(9, 0);
	reg->dpu_vsp_gfree_wait_delay_cfg0 = para << 10 | temp;
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void set_dpu_freq_upd_en_byp(struct dpu_dvfs *dpu, bool enable)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	if (enable)
		reg->dpu_vsp_freq_update_bypass |= BIT(0);
	else
		reg->dpu_vsp_freq_update_bypass &= ~BIT(0);
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void set_dpu_freq_upd_delay_en(struct dpu_dvfs *dpu, bool enable)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	if (enable)
		reg->dpu_vsp_freq_upd_type_cfg |= BIT(5);
	else
		reg->dpu_vsp_freq_upd_type_cfg &= ~BIT(5);
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void set_dpu_freq_upd_hdsk_en(struct dpu_dvfs *dpu, bool enable)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	if (enable)
		reg->dpu_vsp_freq_upd_type_cfg |= BIT(4);
	else
		reg->dpu_vsp_freq_upd_type_cfg &= ~BIT(4);
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void set_dpu_dvfs_swtrig_en(struct dpu_dvfs *dpu, bool enable)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	if (enable)
		reg->dpu_vsp_sw_trig_ctrl |= BIT(0);
	else
		reg->dpu_vsp_sw_trig_ctrl &= ~BIT(0);
	mutex_unlock(&dpu->apsys->reg_lock);
}

static void set_dpu_dvfs_table(struct ip_dvfs_map_cfg *dvfs_table)
{
	memcpy(map_table, dvfs_table, sizeof(struct
		ip_dvfs_map_cfg));
}

static int get_dpu_dvfs_table(struct ip_dvfs_map_cfg *dvfs_table)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		dvfs_table[i].map_index = map_table[i].map_index;
		dvfs_table[i].volt_level = map_table[i].volt_level;
		dvfs_table[i].clk_level = map_table[i].clk_level;
		dvfs_table[i].clk_rate = map_table[i].clk_rate;
	}

	return i;
}

static void get_dpu_dvfs_status(struct dpu_dvfs *dpu, struct ip_dvfs_status *dvfs_status)
{
	struct dpu_vspsys_dvfs_reg *reg =
		(struct dpu_vspsys_dvfs_reg *)dpu->apsys->apsys_base;

	mutex_lock(&dpu->apsys->reg_lock);
	dvfs_status->apsys_cur_volt =
		qogirn6pro_apsys_val_to_volt(reg->dpu_vsp_dvfs_voltage_dbg >> 12 & 0x7);
	dvfs_status->vsp_vote_volt =
		qogirn6pro_apsys_val_to_volt(reg->dpu_vsp_dvfs_voltage_dbg >> 6 & 0x7);
	dvfs_status->dpu_vote_volt =
		qogirn6pro_apsys_val_to_volt(reg->dpu_vsp_dvfs_voltage_dbg >> 3 & 0x7);
	dvfs_status->vdsp_vote_volt = "N/A";

	dvfs_status->vsp_cur_freq = "N/A";
	dvfs_status->dpu_cur_freq =
		qogirn6pro_dpu_val_to_freq(reg->dpu_vsp_vpu_dispc0_dvfs_cgm_cfg_dbg & 0x7);
	dvfs_status->vdsp_cur_freq = "N/A";
	mutex_unlock(&dpu->apsys->reg_lock);
}

static int dpu_dvfs_parse_dt(struct dpu_dvfs *dpu,
				struct device_node *np)
{
	int ret;

	pr_info("%s()\n", __func__);

	ret = of_property_read_u32(np, "sprd,gfree-wait-delay",
			&dpu->dvfs_coffe.gfree_wait_delay);
	if (ret)
		dpu->dvfs_coffe.gfree_wait_delay = 0x100;

	ret = of_property_read_u32(np, "sprd,freq-upd-hdsk-en",
			&dpu->dvfs_coffe.freq_upd_hdsk_en);
	if (ret)
		dpu->dvfs_coffe.freq_upd_hdsk_en = 1;

	ret = of_property_read_u32(np, "sprd,freq-upd-delay-en",
			&dpu->dvfs_coffe.freq_upd_delay_en);
	if (ret)
		dpu->dvfs_coffe.freq_upd_delay_en = 1;

	ret = of_property_read_u32(np, "sprd,freq-upd-en-byp",
			&dpu->dvfs_coffe.freq_upd_en_byp);
	if (ret)
		dpu->dvfs_coffe.freq_upd_en_byp = 0;

	ret = of_property_read_u32(np, "sprd,sw-trig-en",
			&dpu->dvfs_coffe.sw_trig_en);
	if (ret)
		dpu->dvfs_coffe.sw_trig_en = 0;

	return ret;
}

static int dpu_dvfs_init(struct dpu_dvfs *dpu)
{
	void __iomem *base = (void __iomem *)dpu->apsys->top_base;
	char chip_type[HWFEATURE_STR_SIZE_LIMIT];
	u32 temp;

	pr_info("%s()\n", __func__);

	if (dpu_vsp_dvfs_check_clkeb()) {
		pr_info("%s(), dpu_vsp eb is not on\n", __func__);
		return 0;
	}

	dpu_dvfs_map_cfg(dpu);
	set_dpu_gfree_wait_delay(dpu, dpu->dvfs_coffe.gfree_wait_delay);
	set_dpu_freq_upd_hdsk_en(dpu, dpu->dvfs_coffe.freq_upd_hdsk_en);
	set_dpu_freq_upd_delay_en(dpu, dpu->dvfs_coffe.freq_upd_delay_en);
	set_dpu_freq_upd_en_byp(dpu, dpu->dvfs_coffe.freq_upd_en_byp);
	set_dpu_work_index(dpu, dpu->dvfs_coffe.work_index_def);
	set_dpu_idle_index(dpu, dpu->dvfs_coffe.idle_index_def);

	sprd_kproperty_get("auto/chipid", chip_type, "-1");

	writel_relaxed(0x00d000d0, base + 0x3d0);
	temp = readl_relaxed(base + 0xd84);
	temp &= 0xfffffffe;
	writel_relaxed(temp, base + 0xd84);
	writel_relaxed(0x0, base + 0x39c);

	dpu->dvfs_coffe.hw_dfs_en = 1;

	dpu_hw_dfs_en(dpu, dpu->dvfs_coffe.hw_dfs_en);

	return 0;
}

const struct dpu_dvfs_ops qogirn6pro_dpu_dvfs_ops = {
	.parse_dt = dpu_dvfs_parse_dt,
	.dvfs_init = dpu_dvfs_init,
	.hw_dfs_en = dpu_hw_dfs_en,
	.set_work_freq = set_dpu_work_freq,
	.get_work_freq = get_dpu_work_freq,
	.set_idle_freq = set_dpu_idle_freq,
	.get_idle_freq = get_dpu_idle_freq,
	.set_work_index = set_dpu_work_index,
	.get_work_index = get_dpu_work_index,
	.set_idle_index = set_dpu_idle_index,
	.get_idle_index = get_dpu_idle_index,
	.set_dvfs_table = set_dpu_dvfs_table,
	.get_dvfs_table = get_dpu_dvfs_table,
	.get_dvfs_status = get_dpu_dvfs_status,

	.set_gfree_wait_delay = set_dpu_gfree_wait_delay,
	.set_freq_upd_en_byp = set_dpu_freq_upd_en_byp,
	.set_freq_upd_delay_en = set_dpu_freq_upd_delay_en,
	.set_freq_upd_hdsk_en = set_dpu_freq_upd_hdsk_en,
	.set_dvfs_swtrig_en = set_dpu_dvfs_swtrig_en,
};
