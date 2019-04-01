#include <linux/kernel.h>
#include <linux/module.h>

#include "nmictrl.h"

#include "define.h"

static const char nmdbg_driver_name[] = NMDBG_MODULE_NAME;
static const char nmdbg_driver_ver[] = NMDBG_MODULE_VER "_" NMDBG_MODULE_MVER;
static const char nmdbg_driver_desc[] = NMDBG_MODULE_DESC;
static const char nmdbg_driver_copyright[] = "Copyright (c) " NMDBG_MODULE_DATE " " NMDBG_MODULE_AUTHOR " " NMDBG_MODULE_AUTHINFO;

static nmictrl_ret_t nmdbg_test_fn(struct pt_regs * regs)
{
	pr_info("TEST_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}

static nmictrl_ret_t nmdbg_test2_fn(struct pt_regs * regs)
{
	pr_info("TEST2_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}

static int __init nmdbg_init(void)
{
	pr_info("%s - v%s\n", nmdbg_driver_name, nmdbg_driver_ver );
	pr_info("%s\n", nmdbg_driver_desc );
	pr_info("%s\n", nmdbg_driver_copyright );

	nmictrl_add_handler("test_handler", &nmdbg_test_fn);
	nmictrl_add_handler("test2_handler", &nmdbg_test2_fn);
	nmictrl_trigger("test_handler");
	return 0;
}

static void __exit nmdbg_exit(void)
{
	nmictrl_trigger("test2_handler");
	nmictrl_del_handler("test_handler");
	nmictrl_del_handler("test2_handler");
	return;
}

module_init(nmdbg_init);
module_exit(nmdbg_exit);

MODULE_VERSION(NMDBG_MODULE_VER);
MODULE_LICENSE(NMDBG_MODULE_LICENSE);
MODULE_AUTHOR(NMDBG_MODULE_AUTHOR);
MODULE_DESCRIPTION(NMDBG_MODULE_DESC);