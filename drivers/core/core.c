#include <linux/kernel.h>
#include <linux/module.h>

#include "nmictrl.h"

#include "define.h"

static const char nmdbg_driver_name[] = NMDBG_MODULE_NAME;
static const char nmdbg_driver_ver[] = NMDBG_MODULE_VER "_" NMDBG_MODULE_MVER;
static const char nmdbg_driver_desc[] = NMDBG_MODULE_DESC;
static const char nmdbg_driver_copyright[] = "Copyright (c) " NMDBG_MODULE_DATE " " NMDBG_MODULE_AUTHOR " " NMDBG_MODULE_AUTHINFO;

static int __init nmdbg_init(void)
{
	pr_info("%s - v%s\n", nmdbg_driver_name, nmdbg_driver_ver );
	pr_info("%s\n", nmdbg_driver_desc );
	pr_info("%s\n", nmdbg_driver_copyright );
	return 0;
}

static void __exit nmdbg_exit(void)
{
	return;
}

module_init(nmdbg_init);
module_exit(nmdbg_exit);

MODULE_VERSION(NMDBG_MODULE_VER);
MODULE_LICENSE(NMDBG_MODULE_LICENSE);
MODULE_AUTHOR(NMDBG_MODULE_AUTHOR);
MODULE_DESCRIPTION(NMDBG_MODULE_DESC);