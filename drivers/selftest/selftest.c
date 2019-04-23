#include <linux/kernel.h>
#include <linux/module.h>

#include "selftest_nmictrl.h"

static int __init selftest_nmdbg_init(void)
{
	KTX_RUN(selftest_nmictrl);
	return 0;
}

static void __exit selftest_nmdbg_exit(void)
{
	KTX_REPORT(selftest_nmictrl);
	return;
}

module_init(selftest_nmdbg_init);
module_exit(selftest_nmdbg_exit);

MODULE_VERSION("selftest_" NMDBG_MODULE_VER);
MODULE_LICENSE(NMDBG_MODULE_LICENSE);
MODULE_AUTHOR(NMDBG_MODULE_AUTHOR);
MODULE_DESCRIPTION("Selftests for " NMDBG_MODULE_DESC);