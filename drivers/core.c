#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/delay.h>

#include "nmictrl.h"

#include "define.h"

static const char nmdbg_driver_name[] = NMDBG_MODULE_NAME;
static const char nmdbg_driver_ver[] = NMDBG_MODULE_VER "_" NMDBG_MODULE_MVER;
static const char nmdbg_driver_desc[] = NMDBG_MODULE_DESC;
static const char nmdbg_driver_copyright[] = "Copyright (c) " NMDBG_MODULE_DATE " " NMDBG_MODULE_AUTHOR " " NMDBG_MODULE_AUTHINFO;

static nmictrl_ret_t nmdbg_test_self_fn(struct pt_regs * regs)
{
	pr_info("TEST_SELF_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}

static nmictrl_ret_t nmdbg_test_all_fn(struct pt_regs * regs)
{
	pr_info("TEST_ALL_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}

static nmictrl_ret_t nmdbg_test_diffcpu_fn(struct pt_regs * regs)
{
	pr_info("TEST_DIFFCPU_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}

static nmictrl_ret_t nmdbg_test_another_fn(struct pt_regs * regs)
{
	pr_info("TEST_ANOTHER_HANDLER_CALLED!\n");
	return NMICTRL_HANDLED;
}
static nmictrl_ret_t nmdbg_test_shutdown_fn(struct pt_regs * regs)
{
	return NMICTRL_HANDLED;
}

static int __init nmdbg_init(void)
{
	pr_info("%s - v%s\n", nmdbg_driver_name, nmdbg_driver_ver );
	pr_info("%s\n", nmdbg_driver_desc );
	pr_info("%s\n", nmdbg_driver_copyright );

	nmictrl_init();

	nmictrl_add_handler("test_handler_self", &nmdbg_test_self_fn);
	nmictrl_add_handler("test_handler_all", &nmdbg_test_all_fn);
	nmictrl_add_handler("test_handler_diffcpu", &nmdbg_test_diffcpu_fn);
	nmictrl_add_handler("test_handler_another", &nmdbg_test_another_fn);
	nmictrl_add_handler("test_handler_shutdown", &nmdbg_test_shutdown_fn);

	nmictrl_prepare_handler("test_handler_self", smp_processor_id());
	nmictrl_trigger_self();
	nmictrl_prepare_handler("test_handler_all", smp_processor_id());
	nmictrl_trigger_all();
	nmictrl_prepare_handler("test_handler_diffcpu", smp_processor_id());
	return 0;
}

static void __exit nmdbg_exit(void)
{
	nmictrl_trigger_all();
	if ( num_online_cpus() > 1 )
	{
		unsigned int processor_id = smp_processor_id();

		processor_id += (processor_id == 0) ? 1 : -1;
		nmictrl_prepare_handler("test_handler_another", processor_id);
		nmictrl_trigger_others();
	}
	mdelay(1);
	nmictrl_del_handler("test_handler_self");
	nmictrl_del_handler("test_handler_all");
	nmictrl_del_handler("test_handler_diffcpu");
	nmictrl_del_handler("test_handler_another");

	nmictrl_shutdown_sync();
	return;
}

module_init(nmdbg_init);
module_exit(nmdbg_exit);

MODULE_VERSION(NMDBG_MODULE_VER);
MODULE_LICENSE(NMDBG_MODULE_LICENSE);
MODULE_AUTHOR(NMDBG_MODULE_AUTHOR);
MODULE_DESCRIPTION(NMDBG_MODULE_DESC);