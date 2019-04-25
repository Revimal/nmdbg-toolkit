#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "nmictrl.h"
#include "panichook.h"

#include "define.h"

#define NMDBG_SUBSYSTEM_SYNC_TIMEOUT \
	USEC_PER_SEC

static const char nmdbg_driver_name[] = NMDBG_MODULE_NAME;
static const char nmdbg_driver_ver[] = NMDBG_MODULE_VER "_" NMDBG_MODULE_MVER;
static const char nmdbg_driver_desc[] = NMDBG_MODULE_DESC;
static const char nmdbg_driver_copyright[] = "Copyright (c) " NMDBG_MODULE_DATE " " NMDBG_MODULE_AUTHOR " " NMDBG_MODULE_AUTHINFO;

static int __init nmdbg_init(void)
{
	pr_info("%s - v%s\n", nmdbg_driver_name, nmdbg_driver_ver );
	pr_info("%s\n", nmdbg_driver_desc );
	pr_info("%s\n", nmdbg_driver_copyright );

	if (!!nmictrl_startup()) {
		pr_info("Failed to start the nmictrl system");
		goto err;
	}

	panichook_member_init();

	if (!!nmictrl_add_handler("panichook_attach", &panichook_attach_nmifn)) {
		pr_info("Failed to add the panichook_attach handler");
		goto err;
	}
	if (!!nmictrl_add_handler("panichook_detach", &panichook_detach_nmifn)) {
		pr_info("Failed to add the panichook_detach handler");
		goto err;
	}

	nmictrl_prepare_handler("panichook_attach", smp_processor_id());
	nmictrl_trigger_self();

	if (!!panichook_sync_attach(NMDBG_SUBSYSTEM_SYNC_TIMEOUT)) {
		pr_info("Failed to sync panichook_attach due to timeout");
		goto err;
	}

	return 0;

err:
	return -1;
}

static void __exit nmdbg_exit(void)
{
	nmictrl_prepare_handler("panichook_detach", smp_processor_id());
	nmictrl_trigger_self();
	(void) panichook_sync_detach(NMDBG_SUBSYSTEM_SYNC_TIMEOUT);
	nmictrl_shutdown_sync();
	return;
}

module_init(nmdbg_init);
module_exit(nmdbg_exit);

MODULE_VERSION(NMDBG_MODULE_VER);
MODULE_LICENSE(NMDBG_MODULE_LICENSE);
MODULE_AUTHOR(NMDBG_MODULE_AUTHOR);
MODULE_DESCRIPTION(NMDBG_MODULE_DESC);