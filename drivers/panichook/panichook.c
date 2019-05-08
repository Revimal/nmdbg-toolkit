/**
 * @file panichook.c
 * @brief The panichook subsystem.
 *
 * This is implementations of 'panichook subsystem'
 *
 * @author Hyeonho Seo (Revimal)
 * @bug No Known Bugs
 */

#include "panichook.h"

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <linux/delay.h>

#include "define.h"

/**
 * @brief Internal structure for user-defined handler.
 */
typedef struct {
	/** Handler name */
	char handler_name[PANICHOOK_HANDLER_NAMESZ];
	/** Handler function */
	nmictrl_fn_t handler_fn;
	/** Handler list */
	struct list_head handler_list;
	/** Handler RCU object */
	struct rcu_head handler_rcu;
} nmictrl_handler_t;

typedef void (*panichook_fentry_kfn_t)(void);
typedef void (*panichook_panic_kfn_t)(const u8 *fmt, ...);
typedef void (*panichook_oops_kfn_t)(void);

static panichook_fentry_kfn_t panichook_fentry_kfn = NULL;
static panichook_panic_kfn_t panichook_panic_kfn = NULL;
static panichook_oops_kfn_t panichook_oops_kfn = NULL;

/* call xx xx xx xx */
static const u8 panichook_call_rel32_opcode = 0xe8;
static const s32 panichook_call_rel32_size = 5;

static int panichook_modified_panic = 0;
static u8 panichook_opcodes_panic[5] = {0x00, };

static int panichook_modified_oops = 0;
static u8 panichook_opcodes_oops[5] = {0x00, };

/**
 * @brief Internal function to lookup the address of kernel function.
 */
static __always_inline void * panichook_resolve_kfn_symbol(const char *sym)
{
	return (void *)kallsyms_lookup_name(sym);
}

/**
 * @brief Internal function to un-protect a R/O memory range by unsetting WP flags from the CR0 register.
 *
 * This function must be called in a NMI interrupt context because the CR0 register must be protected from preemption.
 */
static __always_inline void panichook_privilege_write_begin(void)
{
	unsigned long cr0 = read_cr0();
	if (cr0 & X86_CR0_WP)
		write_cr0(cr0 ^ X86_CR0_WP);
}

/**
 * @brief Internal function to re-protect a R/O memory range by setting WP flags to the CR0 register.
 *
 * This function must be called in a NMI interrupt context because the CR0 register must be protected from preemption.
 */
static __always_inline void panichook_privilege_write_end(void)
{
	unsigned long cr0 = read_cr0();
	if (!(cr0 & X86_CR0_WP))
		write_cr0(cr0 ^ X86_CR0_WP);
}

/**
 * @brief Internal function to test if an address can be hooked.
 *
 * @param kfn
 * 	address to test
 * @return
 * 	0 if function is hookable.
 */
static int panichook_test_kfn_hookable(void *kfn)
{
	/*
	 * 'nop_type_fentry_opcodes' stores opcodes of 'nopw 0x0(%rax,%rax,1)'.
	 * In a neutral state, __fentry__ is presentated as 'call __fentry__' (near relative call).
	 * But, when vmlinux loaded, __fentry__ could be transformed as a set of opcodes like above.
	 */
	static const u8 nop_type_fentry_opcode[5] = {0x0f, 0x1f, 0x44, 0x00, 0x00};
	/*
	 * Calculate signed-32bit relative offsets for the __fentry__ and a given address.
	 * And store it in 'call_operand'.
	 */
	s32 call_operand =
		(s32)((unsigned long)panichook_fentry_kfn - (unsigned long)kfn) - panichook_call_rel32_size;

	if (kfn == NULL || panichook_fentry_kfn == NULL)
		return -1;

	/*
	 * Case 1. 'call __fentry__'
	 */
	if (((u8 *)kfn)[0] == panichook_call_rel32_opcode &&
		*(s32 *)((u8 *)kfn + 1) == call_operand)
		return 0;
	/*
	 * Case 2. 'nopw 0x0(%rax,%rax,1)'
	 */
	else
		if (((u8 *)kfn)[0] == nop_type_fentry_opcode[0] &&
			((u8 *)kfn)[1] == nop_type_fentry_opcode[1] &&
			((u8 *)kfn)[2] == nop_type_fentry_opcode[2] &&
			((u8 *)kfn)[3] == nop_type_fentry_opcode[3] &&
			((u8 *)kfn)[4] == nop_type_fentry_opcode[4])
			return 0;

	return -1;
}

/**
 * @brief Internal function to handle a kernel panic.
 */
static void panichook_generic_handler(void)
{
	pr_info("PANIC_HANDLED!!!!!!!!!!!!!!!!!!\n");
	while (1)
		cpu_relax();
}

/**
 * @brief Internal function to modify __fentry__ of 'panic()' for the panichook.
 *
 * @param panic_kfn
 * 	address of 'panic()' function
 * @return
 * 	0 if hooking success.
 */
static int panichook_modify_kfn_panic(void *panic_kfn)
{
	s32 call_operand =
			(s32)((unsigned long)&panichook_generic_handler - (unsigned long)panic_kfn) - panichook_call_rel32_size;

	if (!!panichook_modified_panic)
		goto err;

	if (!!probe_kernel_read(panichook_opcodes_panic, panic_kfn, panichook_call_rel32_size))
		goto err;

	panichook_privilege_write_begin();
	smp_mb();
	if (!!probe_kernel_write(panic_kfn, &panichook_call_rel32_opcode, 1) ||
		!!probe_kernel_write((void *)((u8 *)panic_kfn + 1), &call_operand, 4))
		goto priv_err;
	panichook_privilege_write_end();

	panichook_modified_panic = 1;
	return 0;

priv_err:
	panichook_privilege_write_end();
err:
	return -1;
}

/**
 * @brief Internal function to modify __fentry__ of 'oops_enter()' for the panichook.
 *
 * @param oops_kfn
 * 	address of 'oops_enter()' function
 * @return
 * 	0 if hooking success.
 */
static int panichook_modify_kfn_oops(void *oops_kfn)
{
	s32 call_operand =
			(s32)((unsigned long)&panichook_generic_handler - (unsigned long)oops_kfn) - panichook_call_rel32_size;

	if (!!panichook_modified_oops)
		goto err;

	if (!!probe_kernel_read(panichook_opcodes_oops, oops_kfn, panichook_call_rel32_size))
		goto err;

	panichook_privilege_write_begin();
	smp_mb();
	if (!!probe_kernel_write(oops_kfn, &panichook_call_rel32_opcode, 1) ||
		!!probe_kernel_write((void *)((u8 *)oops_kfn + 1), &call_operand, 4))
		goto priv_err;
	panichook_privilege_write_end();

	panichook_modified_oops = 1;
	return 0;

priv_err:
	panichook_privilege_write_end();
err:
	return -1;
}

/**
 * @brief Internal function to recover the original __fentry__ of 'panic()'.
 *
 * @param panic_kfn
 * 	address of 'panic()' function
 */
static void panichook_recover_kfn_panic(void *panic_kfn)
{
	if (!panichook_modified_panic)
		return;

	panichook_privilege_write_begin();
	probe_kernel_write(panic_kfn, panichook_opcodes_panic, panichook_call_rel32_size);
	panichook_privilege_write_end();

	panichook_modified_panic = 0;
}

/**
 * @brief Internal function to recover the original __fentry__ of 'oops_enter()'.
 *
 * @param oops_kfn
 * 	address of 'oops_kfn()' function
 */
static void panichook_recover_kfn_oops(void *oops_kfn)
{
	if (!panichook_modified_oops)
		return;

	panichook_privilege_write_begin();
	probe_kernel_write(oops_kfn, panichook_opcodes_oops, panichook_call_rel32_size);
	panichook_privilege_write_end();

	panichook_modified_oops = 0;
}

void panichook_member_init(void)
{
	panichook_fentry_kfn = panichook_resolve_kfn_symbol("__fentry__");
	panichook_panic_kfn = panichook_resolve_kfn_symbol("panic");
	panichook_oops_kfn = panichook_resolve_kfn_symbol("oops_enter");
}

nmictrl_ret_t panichook_attach_nmifn(struct pt_regs *regs)
{
	if (panichook_fentry_kfn == NULL ||
		panichook_panic_kfn == NULL ||
		panichook_oops_kfn == NULL)
		goto err;

	if (!!panichook_test_kfn_hookable(panichook_panic_kfn))
		goto err;

	if (!!panichook_modify_kfn_panic(panichook_panic_kfn))
		goto err;

	if (!!panichook_test_kfn_hookable(panichook_oops_kfn))
		goto err_on_oops;

	if (!!panichook_modify_kfn_oops(panichook_oops_kfn))
		goto err_on_oops;

err:
	return NMICTRL_HANDLED;

err_on_oops:
	panichook_recover_kfn_panic(panichook_panic_kfn);
	goto err;
}

nmictrl_ret_t panichook_detach_nmifn(struct pt_regs *regs)
{
	if (panichook_fentry_kfn == NULL ||
		panichook_panic_kfn == NULL ||
		panichook_oops_kfn == NULL)
		goto err;

	panichook_recover_kfn_panic(panichook_panic_kfn);
	panichook_recover_kfn_oops(panichook_oops_kfn);

err:
	return NMICTRL_HANDLED;
}

int panichook_sync_attach(unsigned long timeout)
{
	while(!!(timeout--) &&
		(!panichook_modified_panic ||
			!panichook_modified_oops))
		udelay(1);
	if (!timeout)
		return -1;

	pr_info("Attached the panichook subsystem successfully (panic: %p, oops: %p, generic: %p)\n",
		panichook_panic_kfn, panichook_oops_kfn, &panichook_generic_handler);
	return 0;
}

int panichook_sync_detach(unsigned long timeout)
{
	while(!!(timeout--) &&
		(!!panichook_modified_panic ||
			!!panichook_modified_oops))
		udelay(1);
	if (!timeout)
		return -1;
	pr_info("Detached the panichook subsystem successfully (panic: %p, oops: %p, generic: %p)\n",
		panichook_panic_kfn, panichook_oops_kfn, &panichook_generic_handler);
	return 0;
}