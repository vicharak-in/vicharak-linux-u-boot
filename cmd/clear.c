/*
 * Copyright (c) 2025 VICHARAK PVT. LTD.
 * Author: Ajit Singh
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>

static int do_clear(cmd_tbl_t *cmdtp, int flag, int argc,
			 char *const argv[])
{
	printf("\e[2J");
	return 0;
}

U_BOOT_CMD(
	clear, 1, 1,	do_clear,
	"clear all text", ""
);

