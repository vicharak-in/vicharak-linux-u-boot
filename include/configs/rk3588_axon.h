/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2023 Vicharak India
 */

#ifndef __RK3588_AXON_H
#define __RK3588_AXON_H

#include <configs/vicharak_common.h>

#define ROCKCHIP_DEVICE_SETTINGS \
		"stdout=serial,vidconsole\0" \
		"stderr=serial,vidconsole\0" \
		"stdin=usbkbd,serial\0" \
		VICHARAK_BOOT_MENU

#define CONFIG_SYS_MMC_ENV_DEV		1	/* eMMC */

#include <configs/rk3588_common.h>

#endif /* __RK3588_AXON_H */
