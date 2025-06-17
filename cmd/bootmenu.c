/*
 * (C) Copyright 2011-2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <ansi.h>
#include <menu.h>
#include <watchdog.h>
#include <malloc.h>
#include <linux/string.h>

/* maximum bootmenu entries */
#define MAX_COUNT	99

/* maximal size of bootmenu env
 *  9 = strlen("bootmenu_")
 *  2 = strlen(MAX_COUNT)
 *  1 = NULL term
 */
#define MAX_ENV_SIZE	(9 + 2 + 1)

struct bootmenu_entry {
	unsigned short int num;		/* unique number 0 .. MAX_COUNT */
	char key[3];			/* key identifier of number */
	char *title;			/* title of entry */
	char *command;			/* hush command of entry */
	char *optional;			/* any optional data e.g. boot target */
	struct bootmenu_data *menu;	/* this bootmenu */
	struct bootmenu_entry *next;	/* next menu entry (num+1) */
};

struct bootmenu_data {
	int delay;			/* delay for autoboot */
	int active;			/* active menu entry */
	int count;			/* total count of menu entries */
	struct bootmenu_entry *default_entry;	/* default menu entry */
	struct bootmenu_entry *first;	/* first menu entry */
};

enum bootmenu_key {
	KEY_NONE = 0,
	KEY_UP,
	KEY_DOWN,
	KEY_SELECT,
	KEY_DEFAULT_BOOT,
	KEY_EXIT,
};

static char *bootmenu_getoption(unsigned short int n)
{
	char name[MAX_ENV_SIZE];

	if (n > MAX_COUNT)
		return NULL;

	sprintf(name, "bootmenu_%d", n);
	return env_get(name);
}

static void bootmenu_print_entry(void *data)
{
	struct bootmenu_entry *entry = data;
	int active = (entry->menu->active == entry->num);

	/*
	 * Move cursor to line where the entry will be drown (entry->num)
	 * First 5 lines contain bootmenu header + 1 empty line
	 */
	printf(ANSI_CURSOR_POSITION, entry->num + 7, 1);
	puts(ANSI_CLEAR_LINE); /* clear trailing text from previous entry */

	puts("     ");

	if (active){
		puts("\b\b\b-> ");
	}

	if (entry->menu->default_entry == entry)
		printf("%s        <Default>", entry->title);
	else
		puts(entry->title);
}

static void bootmenu_get_default_boot_target(struct bootmenu_data *menu) {
	char *boot_targets;
	char *sep;
	int len = 0;
	struct bootmenu_entry *entry;

	entry = menu->first;

	boot_targets = env_get("boot_targets");
	if (!boot_targets)
		goto finish;

	if (!strlen(boot_targets))
		goto finish;

	sep = strchr(boot_targets, ' ');

	if (!sep)
		len = strlen(boot_targets);
	else
		len = sep - boot_targets;


	for (entry = menu->first; entry; entry = entry->next) {
		if (entry->optional && strlen(entry->optional) == len &&
				!strncmp(entry->optional, boot_targets, len)) {
			menu->default_entry = entry;
			break;
		}
	}

finish:
	/* Something wrong happened */
	if(!menu->default_entry)
		menu->default_entry = menu->first;

	return;
}

static int bootmenu_set_default_boot_target(struct bootmenu_entry *entry) {
	char *boot_targets, *new_boot_targets, *default_boot_target;
	char *sep0, *sep1, *ptr;
	int len, total_len;

	default_boot_target = entry->optional;
	if (!default_boot_target || !strlen(default_boot_target))
		return -1;

	/* Workaround: prevent crashes caused by `env save` when the environment
	 * has not been reset with `env default -a`.
	 */
	run_command("env default -a", 0);

	boot_targets = env_get("boot_targets");
	if (!boot_targets)
		return -1;

	total_len = strlen(boot_targets);

	/* Verify given boot device in boot_targets */
	sep0 = strstr(boot_targets, default_boot_target);
	if (!sep0)
		return -1;

	new_boot_targets = malloc(total_len + 1);
	if (!new_boot_targets)
		return -1;

	ptr = new_boot_targets;

	/* Copy default target first */
	len = strlen(default_boot_target);
	memcpy(ptr, default_boot_target, len);
	ptr += len;

	if (sep0 != boot_targets)
		*ptr++ = ' ';

	/* Copy everything before the default target, exclude space at end */
	len = (sep0 - boot_targets) - 1;
	if (len > 0) {
		memcpy(ptr, boot_targets, len);
		ptr += len;
	}

	sep1 = strchr(sep0, ' ');
	if (sep1) {
		/* Skip the default target and copy the remaining string */
		while (*sep1 == ' ') sep1++;
		if (*sep1) {
			*ptr++ = ' ';
			strcpy(ptr, sep1);
			ptr += strlen(sep1);
		}
	}

	*ptr = '\0';

	/* Not calling env_set(), instead directly overwriting */
	strcpy(boot_targets, new_boot_targets);
	run_command("env save" , 0);
	puts(ANSI_CLEAR_LINE);
	printf("## Default boot target: [%s]\n", entry->optional);

	/* set new default entry */
	entry->menu->default_entry = entry;

	free(new_boot_targets);
	return 0;
}

static void bootmenu_autoboot_loop(struct bootmenu_data *menu,
				enum bootmenu_key *key, int *esc)
{
	int i, c;

	if (menu->delay > 0) {
		printf(ANSI_CURSOR_POSITION, menu->count + 8, 1);
		printf("  Hit any key to stop autoboot: %2d ", menu->delay);
	}

	while (menu->delay > 0) {
		for (i = 0; i < 100; ++i) {
			if (!tstc()) {
				WATCHDOG_RESET();
				mdelay(10);
				continue;
			}

			menu->delay = -1;
			c = getc();

			switch (c) {
			case '\e':
				*esc = 1;
				*key = KEY_NONE;
				break;
			case '\r':
				*key = KEY_SELECT;
				break;
			default:
				*key = KEY_NONE;
				break;
			}

			break;
		}

		if (menu->delay < 0)
			break;

		--menu->delay;
		printf("\b\b\b%2d ", menu->delay);
	}

	printf(ANSI_CURSOR_POSITION, menu->count + 8, 1);
	puts(ANSI_CLEAR_LINE);

	if (menu->delay == 0)
		*key = KEY_EXIT;
}

static void bootmenu_loop(struct bootmenu_data *menu,
		enum bootmenu_key *key, int *esc)
{
	int c;

	while (!tstc()) {
		WATCHDOG_RESET();
		mdelay(10);
	}

	c = getc();

	switch (*esc) {
	case 0:
		/* First char of ANSI escape sequence '\e' */
		if (c == '\e') {
			*esc = 1;
			*key = KEY_NONE;
		}

		break;
	case 1:
		/* Second char of ANSI '[' */
		if (c == '[') {
			*esc = 2;
			*key = KEY_NONE;
		} else {
			*esc = 0;
			*key = KEY_EXIT;
		}
		break;
	case 2:
	case 3:
		/* Third char of ANSI (number '1') - optional */
		if (*esc == 2 && c == '1') {
			*esc = 3;
			*key = KEY_NONE;
			break;
		}

		*esc = 0;

		/* ANSI 'A' - key up was pressed */
		if (c == 'A')
			*key = KEY_UP;
		/* ANSI 'B' - key down was pressed */
		else if (c == 'B')
			*key = KEY_DOWN;
		/* other key was pressed */
		else
			*key = KEY_NONE;

		break;
	}

	/* enter key was pressed */
	if (c == '\r')
		*key = KEY_SELECT;

	/* Shift + D was pressed */
	if (c == 'D')
		*key = KEY_DEFAULT_BOOT;
}

static char *bootmenu_choice_entry(void *data)
{
	struct bootmenu_data *menu = data;
	struct bootmenu_entry *iter;
	enum bootmenu_key key = KEY_NONE;
	int esc = 0;
	int i;

	printf(ANSI_CURSOR_POSITION, menu->count + 14, 1);

	while (1) {
		if (menu->delay >= 0) {
			/* Autoboot was not stopped */
			bootmenu_autoboot_loop(menu, &key, &esc);
			/* Reset cursor position */
			printf(ANSI_CURSOR_POSITION, menu->count + 14, 1);
		} else {
			/* Some key was pressed, so autoboot was stopped */
			bootmenu_loop(menu, &key, &esc);
		}

		switch (key) {
		case KEY_UP:
			if (menu->active > 0)
				--menu->active;
			/* no menu key selected, regenerate menu */
			return NULL;
		case KEY_DOWN:
			if (menu->active < menu->count - 1)
				++menu->active;
			/* no menu key selected, regenerate menu */
			return NULL;
		case KEY_SELECT:
			iter = menu->first;
			for (i = 0; i < menu->active; ++i)
				iter = iter->next;
			return iter->key;
		case KEY_DEFAULT_BOOT:
			iter = menu->first;
			for (i = 0; i < menu->active; ++i)
				iter = iter->next;
			bootmenu_set_default_boot_target(iter);
			return NULL;
		case KEY_EXIT:
			run_command("run distro_bootcmd", 0);
			return NULL;
		default:
			break;
		}
	}

	/* never happens */
	debug("bootmenu: this should not happen");
	return NULL;
}

static void bootmenu_destroy(struct bootmenu_data *menu)
{
	struct bootmenu_entry *iter = menu->first;
	struct bootmenu_entry *next;

	while (iter) {
		next = iter->next;
		free(iter->title);
		free(iter->command);
		free(iter->optional);
		free(iter);
		iter = next;
	}
	free(menu);
}

static struct bootmenu_data *bootmenu_create(int delay)
{
	unsigned short int i = 0;
	const char *option;
	struct bootmenu_data *menu;
	struct bootmenu_entry *iter = NULL;

	int len;
	char *sep0, *sep1;
	struct bootmenu_entry *entry;

	menu = malloc(sizeof(struct bootmenu_data));
	if (!menu)
		return NULL;

	menu->delay = delay;
	menu->active = 0;
	menu->first = NULL;

	while ((option = bootmenu_getoption(i))) {
		sep0 = strchr(option, '=');
		if (!sep0) {
			printf("Invalid bootmenu entry: %s\n", option);
			break;
		}

		entry = malloc(sizeof(struct bootmenu_entry));
		if (!entry)
			goto cleanup;

		len = sep0 - option;
		entry->title = malloc(len + 1);
		if (!entry->title) {
			free(entry);
			goto cleanup;
		}
		memcpy(entry->title, option, len);
		entry->title[len] = 0;

		sep1 = strchr(sep0 + 1, '=');
		if (sep1) {
			len = sep1 - (sep0 + 1);

			entry->optional = malloc(len + 1);
			if (!entry->optional) {
				free(entry->title);
				free(entry);
				goto cleanup;
			}
			memcpy(entry->optional, sep0 + 1, len);
			entry->optional[len] = '\0';
		} else {
			sep1 = sep0;
			entry->optional = NULL;
		}

		len = strlen(sep1 + 1);
		entry->command = malloc(len + 1);
		if (!entry->command) {
			free(entry->optional);
			free(entry->title);
			free(entry);
			goto cleanup;
		}

		memcpy(entry->command, sep1 + 1, len);
		entry->command[len] = 0;

		sprintf(entry->key, "%d", i);

		entry->num = i;
		entry->menu = menu;
		entry->next = NULL;

		if (!iter)
			menu->first = entry;
		else
			iter->next = entry;

		iter = entry;
		++i;

		if (i == MAX_COUNT - 1)
			break;
	}

	/* Add U-Boot console entry at the end */
	if (i <= MAX_COUNT - 1) {
		entry = malloc(sizeof(struct bootmenu_entry));
		if (!entry)
			goto cleanup;

		entry->title = strdup("U-Boot console");
		if (!entry->title) {
			free(entry);
			goto cleanup;
		}

		entry->command = strdup("");
		if (!entry->command) {
			free(entry->title);
			free(entry);
			goto cleanup;
		}

		entry->optional = NULL;

		sprintf(entry->key, "%d", i);

		entry->num = i;
		entry->menu = menu;
		entry->next = NULL;

		if (!iter)
			menu->first = entry;
		else
			iter->next = entry;

		iter = entry;
		++i;
	}

	menu->count = i;
	return menu;

cleanup:
	bootmenu_destroy(menu);
	return NULL;
}

static void bootmenu_show(int delay)
{
	int init = 0;
	void *choice = NULL;
	char *title = NULL;
	char *command = NULL;
	struct menu *menu;
	struct bootmenu_data *bootmenu;
	struct bootmenu_entry *iter;
	char *option, *sep;

	/* If delay is 0 do not create menu, just run first entry */
	if (delay == 0) {
		option = bootmenu_getoption(0);
		if (!option) {
			puts("bootmenu option 0 was not found\n");
			return;
		}
		sep = strchr(option, '=');
		if (!sep) {
			puts("bootmenu option 0 is invalid\n");
			return;
		}
		run_command(sep+1, 0);
		return;
	}

	bootmenu = bootmenu_create(delay);
	if (!bootmenu)
		return;

	bootmenu->default_entry = NULL;

	menu = menu_create(NULL, bootmenu->delay, 1, bootmenu_print_entry,
			   bootmenu_choice_entry, bootmenu);
	if (!menu) {
		bootmenu_destroy(bootmenu);
		return;
	}

	for (iter = bootmenu->first; iter; iter = iter->next) {
		if (!menu_item_add(menu, iter->key, iter))
			goto cleanup;
	}

	bootmenu_get_default_boot_target(bootmenu);

	/* Default menu entry is first, if not set */
	menu_default_set(menu, bootmenu->default_entry->key);

	puts(ANSI_CURSOR_HIDE);
	puts(ANSI_CLEAR_CONSOLE);
	printf(ANSI_CURSOR_POSITION, 1, 1);

	init = 1;

	if (menu_get_choice(menu, &choice)) {
		iter = choice;
		title = strdup(iter->title);
		command = strdup(iter->command);
	}

cleanup:
	menu_destroy(menu);
	bootmenu_destroy(bootmenu);

	if (init) {
		puts(ANSI_CURSOR_SHOW);
		puts(ANSI_CLEAR_CONSOLE);
		printf(ANSI_CURSOR_POSITION, 1, 1);
	}

	if (title && command) {
		debug("Starting entry '%s'\n", title);
		free(title);
		if (run_command(command, 0))
			run_command("bootmenu 10\0", 0);
		free(command);
	}

#ifdef CONFIG_POSTBOOTMENU
	run_command(CONFIG_POSTBOOTMENU, 0);
#endif
}

void menu_display_statusline(struct menu *m)
{
	struct bootmenu_entry *entry;
	struct bootmenu_data *menu;

	if (menu_default_choice(m, (void *)&entry) < 0)
		return;

	menu = entry->menu;

	printf(ANSI_CURSOR_POSITION, 1, 1);
	printf("+------------------------------------------------------+\n");
	printf("|               U-Boot Boot Menu (Vicharak)            |\n");
	printf("+------------------------------------------------------+\n");

	puts(ANSI_CLEAR_LINE_TO_END);
	printf(ANSI_CURSOR_POSITION, 5, 1);
	puts(ANSI_CLEAR_LINE);
	puts("  Select the boot target to load your OS:");

	/* First 5 lines are bootmenu header + 3 empty lines between entries */
	printf(ANSI_CURSOR_POSITION, menu->count + 8, 1);
	puts(ANSI_CLEAR_LINE);
	printf(ANSI_CURSOR_POSITION, menu->count + 9, 1);
	puts("  Press UP/DOWN to move, ENTER to select\n");
	puts(ANSI_CLEAR_LINE);
	puts("  Press SHIFT + D to set a default boot target");
	puts(ANSI_CLEAR_LINE_TO_END);
	printf(ANSI_CURSOR_POSITION, menu->count + 12, 1);
	puts(ANSI_CLEAR_LINE);
	printf("+------------------------------------------------------+\n");
}

#ifdef CONFIG_MENU_SHOW
int menu_show(int bootdelay)
{
	bootmenu_show(bootdelay);
	return -1; /* -1 - abort boot and run monitor code */
}
#endif

int do_bootmenu(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	char *delay_str = NULL;
	int delay = 10;

#if defined(CONFIG_BOOTDELAY) && (CONFIG_BOOTDELAY >= 0)
	delay = CONFIG_BOOTDELAY;
#endif

	if (argc >= 2)
		delay_str = argv[1];

	if (!delay_str)
		delay_str = env_get("bootmenu_delay");

	if (delay_str)
		delay = (int)simple_strtol(delay_str, NULL, 10);

	bootmenu_show(delay);
	return 0;
}

U_BOOT_CMD(
	bootmenu, 2, 1, do_bootmenu,
	"ANSI terminal bootmenu",
	"[delay]\n"
	"    - show ANSI terminal bootmenu with autoboot delay"
);
