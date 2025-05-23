#!/bin/bash
#
# Copyright (C) 2020 Rockchip Electronics Co., Ltd
#
# SPDX-License-Identifier:     GPL-2.0+
#

source ./${srctree}/arch/arm/mach-rockchip/fit_nodes.sh

gen_header
gen_uboot_node
gen_bl31_node
if grep -q '^CONFIG_ANDROID_BOOTLOADER=y' .config ; then
gen_bl32_node
fi
gen_mcu_node
gen_loadable_node
gen_kfdt_node
gen_fdt_node
gen_arm64_configurations
