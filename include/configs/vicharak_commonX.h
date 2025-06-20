/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2024 Vicharak India
 */

#ifndef __VICHARAK_COMMON_H
#define __VICHARAK_COMMON_H

#define VICHARAK_BOOT_MENU \
		"bootmenu_0=1. EMMC BOOT=mmc0=run bootcmd_mmc0\0" \
		"bootmenu_1=2. NVME BOOT=nvme0=run bootcmd_nvme0\0" \
		"bootmenu_2=3. USB BOOT=usb0=run bootcmd_usb0\0" \
		"bootmenu_3=4. SDCARD BOOT=mmc1=run bootcmd_mmc1\0"

#endif /* __VICHARAK_COMMON_H */
