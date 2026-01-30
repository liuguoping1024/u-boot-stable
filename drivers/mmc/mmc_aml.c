// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2008, Freescale Semiconductor, Inc
 * Andy Fleming
 *
 * Based vaguely on the Linux code
 */

#include <config.h>
#include <common.h>
#include <command.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <errno.h>
#include <mmc.h>
#include <part.h>
#include <power/regulator.h>
#include <malloc.h>
#include <memalign.h>
#include <linux/list.h>
#include <linux/crc32.h>
#include <rand.h>
#include <div64.h>
#include "mmc_private.h"
#include <amlstorage/emmc_partitions.h>
#include <amlstorage/partition_table.h>
#include <amlogic/storage.h>
#include <amlogic/asm/secure_apb.h>
#include <amlogic/asm/sd_emmc.h>

static int mmc_set_signal_voltage(struct mmc *mmc, uint signal_voltage);

extern int emmc_probe(uint32_t init_flag);

bool emmckey_is_access_range_legal (struct mmc *mmc, ulong start, lbaint_t blkcnt) {
	ulong key_start_blk, key_end_blk;
	u64 key_glb_offset;
	struct partitions * part = NULL;
	struct virtual_partition *vpart = NULL;
	if (IS_MMC(mmc)) {
		vpart = aml_get_virtual_partition_by_name(MMC_KEY_NAME);
		part = aml_get_partition_by_name(MMC_RESERVED_NAME);
		key_glb_offset = part->offset + vpart->offset;
		key_start_blk = (key_glb_offset / MMC_BLOCK_SIZE);
		key_end_blk = ((key_glb_offset + vpart->size) / MMC_BLOCK_SIZE - 1);
		if (!(info_disprotect & DISPROTECT_KEY)) {
			if ((key_start_blk <= (start + blkcnt -1))
				&& (key_end_blk >= start)
				&& (blkcnt != start)) {
				pr_info("%s, keys %ld, keye %ld, start %ld, blkcnt %ld\n",
						mmc->cfg->name, key_start_blk,
						key_end_blk, start, blkcnt);
				pr_err("Emmckey: Access range is illegal!\n");
				return 0;
			}
		}
	}
	return 1;
}

int emmc_boot_chk(struct mmc *mmc)
{
	u32 val = 0;

	if (strcmp(mmc->dev->name, "emmc"))
		return 0;

	val = readl(SEC_AO_SEC_GP_CFG0);
	pr_info("SEC_AO_SEC_GP_CFG0 = %x\n", val);
	if ((val & 0xf) == 0x1)
		return 1;

	return 0;
}

#if CONFIG_IS_ENABLED(MMC_TINY)
static struct mmc mmc_static;
struct mmc *find_mmc_device(int dev_num)
{
	return &mmc_static;
}

void mmc_do_preinit(void)
{
	struct mmc *m = &mmc_static;
#ifdef CONFIG_FSL_ESDHC_ADAPTER_IDENT
	mmc_set_preinit(m, 1);
#endif
	if (m->preinit)
		mmc_start_init(m);
}

struct blk_desc *mmc_get_blk_desc(struct mmc *mmc)
{
	return &mmc->block_dev;
}
#endif

#if !CONFIG_IS_ENABLED(DM_MMC)

#if CONFIG_IS_ENABLED(MMC_UHS_SUPPORT)
static int mmc_wait_dat0(struct mmc *mmc, int state, int timeout)
{
	return -ENOSYS;
}
#endif

__weak int board_mmc_getwp(struct mmc *mmc)
{
	return -1;
}

int mmc_getwp(struct mmc *mmc)
{
	int wp;

	wp = board_mmc_getwp(mmc);

	if (wp < 0) {
		if (mmc->cfg->ops->getwp)
			wp = mmc->cfg->ops->getwp(mmc);
		else
			wp = 0;
	}

	return wp;
}

__weak int board_mmc_getcd(struct mmc *mmc)
{
	return -1;
}
#endif

#ifdef CONFIG_MMC_TRACE
void mmmc_trace_before_send(struct mmc *mmc, struct mmc_cmd *cmd)
{
	pr_info("CMD_SEND:%d\n", cmd->cmdidx);
	pr_info("\t\tARG\t\t\t 0x%08X\n", cmd->cmdarg);
}

void mmmc_trace_after_send(struct mmc *mmc, struct mmc_cmd *cmd, int ret)
{
	int i;
	u8 *ptr;

	if (ret) {
		pr_info("\t\tRET\t\t\t %d\n", ret);
	} else {
		switch (cmd->resp_type) {
		case MMC_RSP_NONE:
			pr_info("\t\tMMC_RSP_NONE\n");
			break;
		case MMC_RSP_R1:
			pr_info("\t\tMMC_RSP_R1,5,6,7 \t 0x%08X \n",
				cmd->response[0]);
			break;
		case MMC_RSP_R1b:
			pr_info("\t\tMMC_RSP_R1b\t\t 0x%08X \n",
				cmd->response[0]);
			break;
		case MMC_RSP_R2:
			pr_info("\t\tMMC_RSP_R2\t\t 0x%08X \n",
				cmd->response[0]);
			pr_info("\t\t          \t\t 0x%08X \n",
				cmd->response[1]);
			pr_info("\t\t          \t\t 0x%08X \n",
				cmd->response[2]);
			pr_info("\t\t          \t\t 0x%08X \n",
				cmd->response[3]);
			pr_info("\n");
			pr_info("\t\t\t\t\tDUMPING DATA\n");
			for (i = 0; i < 4; i++) {
				int j;
				pr_info("\t\t\t\t\t%03d - ", i*4);
				ptr = (u8 *)&cmd->response[i];
				ptr += 3;
				for (j = 0; j < 4; j++)
					pr_info("%02X ", *ptr--);
				pr_info("\n");
			}
			break;
		case MMC_RSP_R3:
			pr_info("\t\tMMC_RSP_R3,4\t\t 0x%08X \n",
				cmd->response[0]);
			break;
		default:
			pr_info("\t\tERROR MMC rsp not supported\n");
			break;
		}
	}
}

#endif

int mmc_get_ext_csd(struct mmc *mmc, u8 *ext_csd)
{
	return mmc_send_ext_csd(mmc, ext_csd);
}

#if !CONFIG_IS_ENABLED(MMC_TINY)
u8 ext_csd_w[] = {191, 187, 185, 183, 179, 178, 177, 175,
					173, 171, 169, 167, 165, 164, 163, 162,
					161, 156, 155, 143, 140, 136, 134, 133,
					132, 131, 62, 59, 56, 52, 37, 34,
					33, 32, 31, 30, 29, 22, 17, 16, 15};

int mmc_set_ext_csd(struct mmc *mmc, u8 index, u8 value)
{
	int ret = -21, i;

	for (i = 0; i < sizeof(ext_csd_w); i++) {
		if (ext_csd_w[i] == index)
			break;
	}
	if (i != sizeof(ext_csd_w))
		ret = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL, index, value);

	return ret;
}


#endif

#if !CONFIG_IS_ENABLED(DM_MMC)
#ifdef MMC_SUPPORTS_TUNING
static int mmc_execute_tuning(struct mmc *mmc, uint opcode)
{
	return -ENOTSUPP;
}
#endif

static int mmc_set_ios(struct mmc *mmc)
{
	int ret = 0;

	if (mmc->cfg->ops->set_ios)
		ret = mmc->cfg->ops->set_ios(mmc);

	return ret;
}
#endif

struct mode_width_tuning {
	enum bus_mode mode;
	uint widths;
#ifdef MMC_SUPPORTS_TUNING
	uint tuning;
#endif
};

#if CONFIG_IS_ENABLED(MMC_IO_VOLTAGE)
int mmc_voltage_to_mv(enum mmc_voltage voltage)
{
	switch (voltage) {
	case MMC_SIGNAL_VOLTAGE_000: return 0;
	case MMC_SIGNAL_VOLTAGE_330: return 3300;
	case MMC_SIGNAL_VOLTAGE_180: return 1800;
	case MMC_SIGNAL_VOLTAGE_120: return 1200;
	}
	return -EINVAL;
}

static int mmc_set_signal_voltage(struct mmc *mmc, uint signal_voltage)
{
	int err;

	if (mmc->signal_voltage == signal_voltage)
		return 0;

	mmc->signal_voltage = signal_voltage;
	err = mmc_set_ios(mmc);
	if (err)
		pr_debug("unable to set voltage (err %d)\n", err);

	return err;
}
#else
static inline int mmc_set_signal_voltage(struct mmc *mmc, uint signal_voltage)
{
	return 0;
}
#endif

#if !CONFIG_IS_ENABLED(DM_MMC)
/* board-specific MMC power initializations. */
__weak void board_mmc_power_init(void)
{
}
#endif

#ifdef CONFIG_CMD_BKOPS_ENABLE
int mmc_set_bkops_enable(struct mmc *mmc)
{
	int err;
	ALLOC_CACHE_ALIGN_BUFFER(u8, ext_csd, MMC_MAX_BLOCK_LEN);

	err = mmc_send_ext_csd(mmc, ext_csd);
	if (err) {
		puts("Could not get ext_csd register values\n");
		return err;
	}

	if (!(ext_csd[EXT_CSD_BKOPS_SUPPORT] & 0x1)) {
		puts("Background operations not supported on device\n");
		return -EMEDIUMTYPE;
	}

	if (ext_csd[EXT_CSD_BKOPS_EN] & 0x1) {
		puts("Background operations already enabled\n");
		return 0;
	}

	err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_BKOPS_EN, 1);
	if (err) {
		puts("Failed to enable manual background operations\n");
		return err;
	}

	puts("Enabled manual background operations\n");

	return 0;
}
#endif

extern unsigned long blk_dwrite(struct blk_desc *block_dev, lbaint_t start,
		lbaint_t blkcnt, const void *buffer);

int mmc_key_write(unsigned char *buf, unsigned int size, uint32_t *actual_lenth)
{
	ulong start, start_blk, blkcnt, ret;
	unsigned char * temp_buf = buf;
	int i = 2, dev = EMMC_DTB_DEV;
	struct partitions * part = NULL;
	struct mmc *mmc;
	struct virtual_partition *vpart = NULL;
	vpart = aml_get_virtual_partition_by_name(MMC_KEY_NAME);
	part = aml_get_partition_by_name(MMC_RESERVED_NAME);

	mmc = find_mmc_device(dev);

	start = part->offset + vpart->offset;
	start_blk = (start / MMC_BLOCK_SIZE);
	blkcnt = (size / MMC_BLOCK_SIZE);
	info_disprotect |= DISPROTECT_KEY;
	do {
		ret = blk_dwrite(mmc_get_blk_desc(mmc), start_blk, blkcnt, temp_buf);
		if (ret != blkcnt) {
			pr_err("[%s] %d, mmc_bwrite error\n",
				__func__, __LINE__);
			return 1;
		}
		start_blk += vpart->size / MMC_BLOCK_SIZE;
	} while (--i);
	info_disprotect &= ~DISPROTECT_KEY;
	return 0;
}

extern unsigned long blk_derase(struct blk_desc *block_dev, lbaint_t start,
		lbaint_t blkcnt);

int mmc_key_erase(void)
{
	ulong start, start_blk, blkcnt, ret;
	struct partitions * part = NULL;
	struct virtual_partition *vpart = NULL;
	struct mmc *mmc;
	vpart = aml_get_virtual_partition_by_name(MMC_KEY_NAME);
	part = aml_get_partition_by_name(MMC_RESERVED_NAME);
	int dev = EMMC_DTB_DEV;

	mmc = find_mmc_device(dev);
	start = part->offset + vpart->offset;
	start_blk = (start / MMC_BLOCK_SIZE);
	blkcnt = (vpart->size / MMC_BLOCK_SIZE) * 2;//key and backup key
	info_disprotect |= DISPROTECT_KEY;
	ret = blk_derase(mmc_get_blk_desc(mmc), start_blk, blkcnt);
	info_disprotect &= ~DISPROTECT_KEY;
	if (ret) {
		pr_err("[%s] %d mmc_berase error\n",
				__func__, __LINE__);
		return 1;
	}
	return 0;
}


int mmc_key_read(unsigned char *buf, unsigned int size, uint32_t *actual_lenth)
{
	ulong start, start_blk, blkcnt, ret;
	int dev = EMMC_DTB_DEV;
	unsigned char *temp_buf = buf;
	struct partitions * part = NULL;
	struct mmc *mmc;
	struct virtual_partition *vpart = NULL;
	vpart = aml_get_virtual_partition_by_name(MMC_KEY_NAME);
	part = aml_get_partition_by_name(MMC_RESERVED_NAME);

	mmc = find_mmc_device(dev);
	*actual_lenth =  0x40000;/*key size is 256KB*/
	start = part->offset + vpart->offset;
	start_blk = (start / MMC_BLOCK_SIZE);
	blkcnt = (size / MMC_BLOCK_SIZE);
	info_disprotect |= DISPROTECT_KEY;
	ret = blk_dread(mmc_get_blk_desc(mmc), start_blk, blkcnt, temp_buf);
	info_disprotect &= ~DISPROTECT_KEY;
	if (ret != blkcnt) {
		pr_err("[%s] %d, mmc_bread error\n",
			__func__, __LINE__);
		return 1;
	}
	return 0;
}



