/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2001-2015
 * DENX Software Engineering -- wd@denx.de
 * Compulab Ltd - http://compulab.co.il/
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <video.h>
#include <video_console.h>
#include <video_font.h>		/* Get font data, width and height */

/* By default we scroll by a single line */
#ifndef CONFIG_CONSOLE_SCROLL_LINES
#define CONFIG_CONSOLE_SCROLL_LINES 1
#endif

void vidconsole_position_cursor(struct udevice *dev, unsigned x, unsigned y);

static char ansi_buf[10];
static size_t ansi_buf_size = 0;
static int ansi_colors_need_revert = 0;

int vidconsole_putc_xy(struct udevice *dev, uint x, uint y, char ch)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->putc_xy)
		return -ENOSYS;
	return ops->putc_xy(dev, x, y, ch);
}

static void vidconsole_position_cursor2(struct udevice *dev, unsigned col,
		unsigned row)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);

	/* if col or row < 0, set it to current cursor postition */
	if (col < 0)
		col = priv->xcur_frac;
	else
		col = col * priv->x_charsize;

	if (row < 0)
		row = priv->ycur;
	else
		row = row * priv->y_charsize;

	vidconsole_position_cursor(dev, col, row);
}

static void vidconsole_clear(struct udevice *dev)
{
	struct video_priv *priv = dev_get_uclass_priv(dev->parent);

	memset(priv->fb, priv->colour_bg, priv->fb_size);
	vidconsole_position_cursor(dev, 0, 0);

	video_sync(dev->parent);
}

static void vidconsole_clear_line(struct udevice *dev, unsigned st_col,
		unsigned end_col, unsigned row)
{
	uint8_t *st_ptr;
	size_t sz;
	int i;

	if (end_col < st_col)
		return;

	struct vidconsole_priv *vidc_priv = dev_get_uclass_priv(dev);
	struct video_priv *priv = dev_get_uclass_priv(dev->parent);

	row = row * vidc_priv->y_charsize;
	st_col = st_col * vidc_priv->x_charsize;
	end_col = end_col * vidc_priv->x_charsize;

	if (end_col >= priv->xsize)
		end_col = priv->xsize - 1;

	for (i=0; i < vidc_priv->y_charsize; i++) {
		st_ptr = priv->fb + ((row + i) * priv->line_length) +
			st_col * VNBYTES(priv->bpix);
		sz = (end_col - st_col + 1) * VNBYTES(priv->bpix);
		memset(st_ptr, 0, sz);
	}
}

int vidconsole_move_rows(struct udevice *dev, uint rowdst, uint rowsrc,
			 uint count)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->move_rows)
		return -ENOSYS;
	return ops->move_rows(dev, rowdst, rowsrc, count);
}

int vidconsole_set_row(struct udevice *dev, uint row, int clr)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->set_row)
		return -ENOSYS;
	return ops->set_row(dev, row, clr);
}

static int vidconsole_entry_start(struct udevice *dev)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->entry_start)
		return -ENOSYS;
	return ops->entry_start(dev);
}

/* Move backwards one space */
static int vidconsole_back(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	int ret;

	if (ops->backspace) {
		ret = ops->backspace(dev);
		if (ret != -ENOSYS)
			return ret;
	}

	priv->xcur_frac -= VID_TO_POS(priv->x_charsize);
	if (priv->xcur_frac < priv->xstart_frac) {
		priv->xcur_frac = (priv->cols - 1) *
			VID_TO_POS(priv->x_charsize);
		priv->ycur -= priv->y_charsize;
		if (priv->ycur < 0)
			priv->ycur = 0;
	}
	video_sync(dev->parent);

	return 0;
}

/* Move to a newline, scrolling the display if necessary */
static void vidconsole_newline(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);
	const int rows = CONFIG_CONSOLE_SCROLL_LINES;
	int i;

	priv->xcur_frac = priv->xstart_frac;
	priv->ycur += priv->y_charsize;

	/* Check if we need to scroll the terminal */
	if ((priv->ycur + priv->y_charsize) / priv->y_charsize > priv->rows) {
		vidconsole_move_rows(dev, 0, rows, priv->rows - rows);
		for (i = 0; i < rows; i++)
			vidconsole_set_row(dev, priv->rows - i - 1,
					   vid_priv->colour_bg);
		priv->ycur -= rows * priv->y_charsize;
	}
	priv->last_ch = 0;

	video_sync(dev->parent);
}

void vidconsole_swap_colours(struct udevice *dev)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	/* swap colours */
	vid_priv->colour_bg = vid_priv->colour_bg ^ vid_priv->colour_fg;
	vid_priv->colour_fg = vid_priv->colour_bg ^ vid_priv->colour_fg;
	vid_priv->colour_bg = vid_priv->colour_bg ^ vid_priv->colour_fg;
}

void vidconsole_cursor_fixup(struct vidconsole_priv *priv)
{
	if (priv->xcur_frac < 0)
		priv->xcur_frac = 0;
	if (priv->xcur_frac >= priv->cols)
		priv->xcur_frac = priv->cols - 1;

	if (priv->ycur < 0)
		priv->ycur = 0;
	if(priv->ycur >=  priv->rows)
		priv->ycur = priv->rows - 1;
}

void vidconsole_cursor_shift(struct vidconsole_priv *priv, int xshift, int yshift)
{
	priv->xcur_frac +=  xshift * priv->x_charsize;
	priv->ycur += yshift * priv->y_charsize;

	vidconsole_cursor_fixup(priv);
}

static int put_char(struct udevice *dev, char ch)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	int ret;

	switch (ch) {
	case '\a':
		/* beep */
		break;
	case '\r':
		priv->xcur_frac = priv->xstart_frac;
		break;
	case '\n':
		vidconsole_newline(dev);
		vidconsole_entry_start(dev);
		break;
	case '\t':	/* Tab (8 chars alignment) */
		priv->xcur_frac = ((priv->xcur_frac / priv->tab_width_frac)
				+ 1) * priv->tab_width_frac;

		if (priv->xcur_frac >= priv->xsize_frac)
			vidconsole_newline(dev);
		break;
	case '\b':
		vidconsole_back(dev);
		priv->last_ch = 0;
		break;
	default:
		/*
		 * Failure of this function normally indicates an unsupported
		 * colour depth. Check this and return an error to help with
		 * diagnosis.
		 */
		ret = vidconsole_putc_xy(dev, priv->xcur_frac, priv->ycur, ch);
		if (ret == -EAGAIN) {
			vidconsole_newline(dev);
			ret = vidconsole_putc_xy(dev, priv->xcur_frac,
						 priv->ycur, ch);
		}
		if (ret < 0)
			return ret;
		priv->xcur_frac += ret;
		priv->last_ch = ch;
		if (priv->xcur_frac >= priv->xsize_frac)
			vidconsole_newline(dev);
		break;
	}

	return 0;
}

/* Reference to code has taken from cfb_console.c */
int vidconsole_put_char(struct udevice *dev, const char ch)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	int i, cur_row, cur_col, ret = 0;

	if (ch == 27) {
		for (i = 0; i < ansi_buf_size; ++i) {
			ret = put_char(dev, ansi_buf[i]);
			if (ret < 0)
				return ret;
		}

		ansi_buf[0] = 27;
		ansi_buf_size = 1;

		return 0;
	}

	if (ansi_buf_size > 0) {
		/*
		 * 0 - ESC
		 * 1 - [
		 * 2 - num1
		 * 3 - ..
		 * 4 - ;
		 * 5 - num2
		 * 6 - ..
		 * - cchar
		 */

		int next = 0;

		int fail  = 0;
		int flush = 0;

		int num1 = 0;
		int num2 = 0;
		int cchar = 0;

		ansi_buf[ansi_buf_size++] = ch;

		if (ansi_buf_size >= sizeof(ansi_buf))
			fail = 1;

		for (i = 0; i < ansi_buf_size; ++i) {
			if (fail)
				break;

			switch (next) {

			case 0:
				if (ansi_buf[i] == 27)
					next = 1;
				else
					fail = 1;
				break;

				case 1:
				if (ansi_buf[i] == '[')
					next = 2;
				else
					fail = 1;
				break;

			case 2:
				if (ansi_buf[i] >= '0' && ansi_buf[i] <= '9') {
					num1 = ansi_buf[i]-'0';
					next = 3;
				} else if (ansi_buf[i] != '?') {
					--i;
					num1 = 1;
					next = 4;
				}
				break;

			case 3:
				if (ansi_buf[i] >= '0' && ansi_buf[i] <= '9') {
					num1 *= 10;
					num1 += ansi_buf[i]-'0';
				} else {
					--i;
					next = 4;
				}
				break;

			case 4:
				if (ansi_buf[i] != ';') {
					--i;
					next = 7;
				} else
					next = 5;
				break;

			case 5:
				if (ansi_buf[i] >= '0' && ansi_buf[i] <= '9') {
					num2 = ansi_buf[i]-'0';
					next = 6;
				} else
					fail = 1;
				break;

			case 6:
				if (ansi_buf[i] >= '0' && ansi_buf[i] <= '9') {
					num2 *= 10;
					num2 += ansi_buf[i]-'0';
				} else {
					--i;
					next = 7;
				}
				break;

			case 7:
				if ((ansi_buf[i] >= 'A' && ansi_buf[i] <= 'H')
					|| ansi_buf[i] == 'J'
					|| ansi_buf[i] == 'K'
					|| ansi_buf[i] == 'h'
					|| ansi_buf[i] == 'l'
					|| ansi_buf[i] == 'm') {
					cchar = ansi_buf[i];
					flush = 1;
				} else
					fail = 1;
				break;
			}
		}

		if (fail) {
			for (i = 0; i < ansi_buf_size; ++i) {
				ret = put_char(dev, ansi_buf[i]);
				if (ret < 0)
					return ret;
			}
			ansi_buf_size = 0;
			return ret;
		}

		if (flush) {
			ansi_buf_size = 0;
			cur_row = priv->ycur / priv->y_charsize;
			cur_col = priv->xcur_frac / priv->x_charsize;

			switch (cchar) {
			case 'A':
				/* move cursor num1 rows up */
				vidconsole_cursor_shift(priv, 0, -num1);
				break;
			case 'B':
				/* move cursor num1 rows down */
				vidconsole_cursor_shift(priv, 0, num1);
				break;
			case 'C':
				/* move cursor num1 columns forward */
				vidconsole_cursor_shift(priv, num1, 0);
				break;
			case 'D':
				/* move cursor num1 columns back */
				vidconsole_cursor_shift(priv, -num1, 0);
				break;
			case 'E':
				/* move cursor num1 rows up at begin of row */
				vidconsole_newline(dev);
				vidconsole_cursor_shift(priv, 0, -(num1 + 1));
				break;
			case 'F':
				/* move cursor num1 rows down at begin of row */
				vidconsole_newline(dev);
				vidconsole_cursor_shift(priv, 0, (num1 -1));
				break;
			case 'G':
				/* move cursor to column num1 */
				vidconsole_position_cursor2(dev, num1, cur_row);
				break;
			case 'H':
				/* move cursor to row num1, column num2 */
				vidconsole_position_cursor2(dev, num2-1, num1-1);
				break;
			case 'J':
				/* clear console and move cursor to 0, 0 */
				vidconsole_clear(dev);
				break;
			case 'K':
				/* clear line */
				if (num1 == 0)
					vidconsole_clear_line(dev, cur_col, priv->cols - 1, cur_row);
				else if (num1 == 1)
					vidconsole_clear_line(dev, 0, cur_col, cur_row);
				else
					vidconsole_clear_line(dev, 0, priv->cols - 1, cur_row);
				break;
			case 'm':
				if (num1 == 0) { /* reset swapped colors */
					if (ansi_colors_need_revert) {
						vidconsole_swap_colours(dev);
						ansi_colors_need_revert = 0;
					}
				} else if (num1 == 7) { /* once swap colors */
					if (!ansi_colors_need_revert) {
						vidconsole_swap_colours(dev);
						ansi_colors_need_revert = 1;
					}
				}
				break;
			}
		}

	} else {
		ret = put_char(dev, ch);
	}

	return ret;
}

static void vidconsole_putc(struct stdio_dev *sdev, const char ch)
{
	struct udevice *dev = sdev->priv;

	vidconsole_put_char(dev, ch);
	video_sync(dev->parent);
}

static void vidconsole_puts(struct stdio_dev *sdev, const char *s)
{
	struct udevice *dev = sdev->priv;

	while (*s)
		vidconsole_put_char(dev, *s++);
	video_sync(dev->parent);
}

/* Set up the number of rows and colours (rotated drivers override this) */
static int vidconsole_pre_probe(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);

	priv->xsize_frac = VID_TO_POS(vid_priv->xsize);

	return 0;
}

/* Register the device with stdio */
static int vidconsole_post_probe(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct stdio_dev *sdev = &priv->sdev;

	if (!priv->tab_width_frac)
		priv->tab_width_frac = VID_TO_POS(priv->x_charsize) * 8;

	if (dev->seq) {
		snprintf(sdev->name, sizeof(sdev->name), "vidconsole%d",
			 dev->seq);
	} else {
		strcpy(sdev->name, "vidconsole");
	}

	sdev->flags = DEV_FLAGS_OUTPUT;
	sdev->putc = vidconsole_putc;
	sdev->puts = vidconsole_puts;
	sdev->priv = dev;

	return stdio_register(sdev);
}

UCLASS_DRIVER(vidconsole) = {
	.id		= UCLASS_VIDEO_CONSOLE,
	.name		= "vidconsole0",
	.pre_probe	= vidconsole_pre_probe,
	.post_probe	= vidconsole_post_probe,
	.per_device_auto_alloc_size	= sizeof(struct vidconsole_priv),
};

void vidconsole_position_cursor(struct udevice *dev, unsigned x, unsigned y)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);

	priv->xcur_frac = VID_TO_POS(min_t(short, x, vid_priv->xsize - 1));
	priv->ycur = min_t(short, y, vid_priv->ysize - 1);
}

static int do_video_setcursor(cmd_tbl_t *cmdtp, int flag, int argc,
			      char *const argv[])
{
	unsigned int col, row;
	struct udevice *dev;

	if (argc != 3)
		return CMD_RET_USAGE;

	if (uclass_first_device_err(UCLASS_VIDEO_CONSOLE, &dev))
		return CMD_RET_FAILURE;
	col = simple_strtoul(argv[1], NULL, 10);
	row = simple_strtoul(argv[2], NULL, 10);
	vidconsole_position_cursor(dev, col, row);

	return 0;
}

static int do_video_puts(cmd_tbl_t *cmdtp, int flag, int argc,
			 char *const argv[])
{
	struct udevice *dev;
	const char *s;

	if (argc != 2)
		return CMD_RET_USAGE;

	if (uclass_first_device_err(UCLASS_VIDEO_CONSOLE, &dev))
		return CMD_RET_FAILURE;

	for (s = argv[1]; *s; s++)
		vidconsole_put_char(dev, *s);

	video_sync(dev->parent);

	return 0;
}

U_BOOT_CMD(
	setcurs, 3,	1,	do_video_setcursor,
	"set cursor position within screen",
	"    <col> <row> in character"
);

U_BOOT_CMD(
	lcdputs, 2,	1,	do_video_puts,
	"print string on video framebuffer",
	"    <string>"
);
