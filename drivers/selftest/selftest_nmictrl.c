#include "selftest_nmictrl.h"

#include <linux/smp.h>
#include <linux/delay.h>

#include "nmictrl.h"

static int selftest_nmictrl_self_flag = 0;
static nmictrl_ret_t selftest_nmictrl_self_testfn(struct pt_regs * regs)
{
	pr_info("TEST_SELF_HANDLER_CALLED!\n");
	selftest_nmictrl_self_flag = 1;
	return NMICTRL_HANDLED;
}

static int selftest_nmictrl_all_flag = 0;
static nmictrl_ret_t selftest_nmictrl_all_testfn(struct pt_regs * regs)
{
	pr_info("TEST_ALL_HANDLER_CALLED!\n");
	selftest_nmictrl_all_flag = 1;
	return NMICTRL_HANDLED;
}

static int selftest_nmictrl_another_flag = 0;
static nmictrl_ret_t selftest_nmictrl_another_testfn(struct pt_regs * regs)
{
	pr_info("TEST_ANOTHER_HANDLER_CALLED!\n");
	selftest_nmictrl_another_flag = 1;
	return NMICTRL_HANDLED;
}

static nmictrl_ret_t selftest_nmictrl_shutdown_testfn(struct pt_regs * regs)
{
	return NMICTRL_HANDLED;
}

KTX_DEFINE(selftest_nmictrl)
{
	KTX_REQUIRE(selftest_nmictrl, nmictrl_init(), 0);
	KTX_REQUIRE(selftest_nmictrl, nmictrl_add_handler("selftest_nmictrl_self", &selftest_nmictrl_self_testfn), 0);
	KTX_REQUIRE(selftest_nmictrl, nmictrl_add_handler("selftest_nmictrl_all", &selftest_nmictrl_all_testfn), 0);
	KTX_REQUIRE(selftest_nmictrl, nmictrl_add_handler("selftest_nmictrl_another", &selftest_nmictrl_another_testfn), 0);
	KTX_REQUIRE(selftest_nmictrl, nmictrl_add_handler("selftest_nmictrl_shutdown", &selftest_nmictrl_shutdown_testfn), 0);

	nmictrl_prepare_handler("selftest_nmictrl_self", smp_processor_id());
	nmictrl_trigger_self();
	nmictrl_prepare_handler("selftest_nmictrl_all", smp_processor_id());
	nmictrl_trigger_all();
	if ( num_online_cpus() > 1 )
	{
		unsigned int processor_id = smp_processor_id();

		processor_id += (processor_id == 0) ? 1 : -1;
		nmictrl_prepare_handler("selftest_nmictrl_another", processor_id);
		nmictrl_trigger_others();
	}
	/* Wait for triggering IPI signals */
	mdelay(1);

	/* Check all handler has called */
	KTX_CHECK(selftest_nmictrl, selftest_nmictrl_self_flag, 1);
	KTX_CHECK(selftest_nmictrl, selftest_nmictrl_all_flag, 1);
	KTX_CHECK(selftest_nmictrl, selftest_nmictrl_another_flag, 1);

	nmictrl_del_handler("selftest_nmictrl_self");
	nmictrl_del_handler("selftest_nmictrl_all");
	nmictrl_del_handler("selftest_nmictrl_another");
	/* 'selftest_nmictrl_shutdown' will automatically be deleted in the shutdown phase. */
	nmictrl_shutdown_sync();
}