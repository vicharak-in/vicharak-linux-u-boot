/*
 * SPDX-License-Identifier:     GPL-2.0+
 *
 * (C) Copyright 2021 Rockchip Electronics Co., Ltd
 */

#include <dm.h>
#include <mmc.h>
#include <blk.h>
#include <console.h>

bool console_magic_match;

/**
 * is_mmc_magic_match - Check if the MMC magic sequence matches
 *
 * Reads a predefined magic sequence from a fixed MMC offset and compares it
 * against the expected bytes. This is used to determine whether console access
 * should be enabled or disabled on the board.
 *
 * Return: true if the magic matches, false otherwise.
 */
static const u8 expected_magic[MMC_MAGIC_BYTES] = {0x80, 0x00, 0x85};

bool is_mmc_magic_match(void)
{
	struct mmc *mmc;
	struct blk_desc *desc;
	u8 buf[512];
	int i;

	mmc = find_mmc_device(0);
	if (!mmc || mmc_init(mmc))
		goto err;

	desc = mmc_get_blk_desc(mmc);
	if (!desc)
		goto err;

	lbaint_t sector = MMC_MAGIC_OFFSET / desc->blksz;

	if (blk_dread(desc, sector, 1, buf) != 1)
		goto err;

	printf("Debug: ");
	/* Compare first MMC_MAGIC_BYTES in buffer with expected magic */
	for (i = 0; i < MMC_MAGIC_BYTES; i++) {
		printf("buf[%d] = 0x%02x, expected = 0x%02x\n", i, buf[i], expected_magic[i]);
		if (buf[i] != expected_magic[i])
			goto err;
	}
	printf("U-Boot console: DISABLED (magic verified)\n");
	return true;
err:
	printf("U-Boot console: ENABLED\n");
	return false;
}
