/*
 * (C) Copyright 2000-2009
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __CONSOLE_H
#define __CONSOLE_H

extern char console_buffer[];

/* common/console.c */
int console_init_f(void);	/* Before relocation; uses the serial  stuff */
int console_init_r(void);	/* After  relocation; uses the console stuff */
int console_assign(int file, const char *devname);	/* Assign the console */
int ctrlc(void);
int had_ctrlc(void);	/* have we had a Control-C since last clear? */
void clear_ctrlc(void);	/* clear the Control-C condition */
int disable_ctrlc(int);	/* 1 to disable, 0 to enable Control-C detect */
int confirm_yesno(void);        /*  1 if input is "y", "Y", "yes" or "YES" */

int ctrlq(void);
int had_ctrlq(void);	/* have we had a Control-Q since last clear? */
void clear_ctrlq(void);	/* clear the Control-Q condition */
int disable_ctrlq(int);	/* 1 to disable, 0 to enable Control-Q detect */

/**
 * ctrlc_or_q() - checks for both ctrl-q or ctrl-c
 *
 * Calling explicitly both ctrlq() & ctrlc() consumes the input for other.
 */
int ctrlc_or_q(void);

/**
 * console_record_init() - set up the console recording buffers
 *
 * This should be called as soon as malloc() is available so that the maximum
 * amount of console output can be recorded.
 */
int console_record_init(void);

/**
 * console_record_reset() - reset the console recording buffers
 *
 * Removes any data in the buffers
 */
void console_record_reset(void);

/**
 * console_record_reset_enable() - reset and enable the console buffers
 *
 * This should be called to enable the console buffer.
 */
void console_record_reset_enable(void);

/**
 * console_record_print_purge() - print record data and remove data from buffers
 */
void console_record_print_purge(void);

/**
 * console_announce_r() - print a U-Boot console on non-serial consoles
 *
 * When U-Boot starts up with a display it generally does not announce itself
 * on the display. The banner is instead emitted on the UART before relocation.
 * This function prints a banner on devices which (we assume) did not receive
 * it before relocation.
 *
 * @return 0 (meaning no errors)
 */
int console_announce_r(void);

/*
 * CONSOLE multiplexing.
 */
#ifdef CONFIG_CONSOLE_MUX
#include <iomux.h>
#endif

#endif
