#include <config.h>
#include <common.h>
#include <command.h>
#include <fs.h>
#include <linux/ctype.h>

static char *fs_argv[5];

int do_fileenv(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc < 6)
		return CMD_RET_USAGE;

	fs_argv[0] = "fatload";
	fs_argv[1] = argv[1];
	fs_argv[2] = argv[2];
	fs_argv[3] = argv[3];
	fs_argv[4] = argv[4];

	if (do_fat_fsload(cmdtp, 0, 5, fs_argv) != 0)
		return 1;

	char *addr = (char *)simple_strtoul(argv[3], NULL, 16);
	size_t size = env_get_hex("filesize", 0);

	// Prepare string
	addr[size] = 0x00;
	char *s = addr;
	while(*s != 0x00) {
		if (isprint(*s)) {
			s++;
		}
		else {
			*s = 0x00;
		}
	}

	return env_set(argv[5], addr);
}

U_BOOT_CMD(
	fileenv, 6, 0, do_fileenv,
	"Read file and store it into env.",
	"<interface> <dev:part> <addr> <filename> <envname>\n"
	"    - Read file from fat32 and store it as env."
);
