/**
 * @file nmictrl.c
 * @brief The NMI control subsystem.
 *
 * This is implementations of 'NMI control subsystem'
 *
 * @author Hyeonho Seo (revimal)
 * @bugs No Known Bugs
 */

#include "nmictrl.h"

#include <asm/apic.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <asm/nmi.h>

#include "define.h"

#define NMICTRL_GENERIC_HANDLER_NAME "nmictrl_generic_handler"

/**
 * @brief User-defined handler structure.
 */
typedef struct {
	char handler_name[NMICTRL_HANDLER_NAMESZ];
	cpumask_t handler_mask;
	nmictrl_fn_t handler_fn;
	struct list_head handler_list;
	struct rcu_head handler_rcu;
} nmictrl_handler_t;

static cpumask_t nmictrl_processor_mask;

static DEFINE_SPINLOCK(nmictrl_global_write_lock);
static LIST_HEAD(nmictrl_handler_list);

/**
 * @brief Wrapper function to handle generated IPI signal.
 *
 * @p cmd will not pass to the user-registered handler, because it always is 'NMI_LOCAL'.
 */
static int nmictrl_generic_handler(unsigned int cmd, struct pt_regs *regs)
{
	nmictrl_ret_t handler_ret;
	nmictrl_handler_t *handler_ptr, *handler_nptr;

	if (cpumask_test_and_clear_cpu(smp_processor_id(), &nmictrl_processor_mask))
	{
		rcu_read_lock();
		/*
		 * This is "Non-maskable" intetrupt handler callback.
		 * It means, your CPU can enter this routine even it in IRQ disabled state.
		 * In that case, we cannot guarantee serialized synchronization using normal RCU APIs on the SMP system.
		 *
		 * Following example illustrate calling 'nmictrl_shutdown_sync' in CPU1 at the same time with NMI self-triggered in CPU0.
		 * If we use normal 'list_for_each_entry_rcu' to iterate prepared handler, a horrible disaster will strike on our system.
		 *
		 * INITIAL LIST STATE:
		 *  [0]: handler_0
		 *  [1]: handler_1 -> Will be executed
		 *  [2]: handler_2
		 *
		 * CPU0 							 CPU1
		 * ---- 							 ----
		 *      							 CALL_SHUTDOWN_SYNC
		 * TRIGGER_LIST_ENTRY[1]
		 *      							 ACQUIRE_IRQ_DISABLE
		 * ENTER_NMI_CONTEXT
		 * ACCESS_LIST_ENTRY[0]
		 *      							 REMOVE_LIST_ENTRY[0]
		 * LIST_FOREACH_BREAK
		 * NOT_CLEANED_CPUMASK[1]
		 *      							 REMOVE_LIST_ENTRY[1]
		 *      							 REMOVE_LIST_ENTRY[2]
		 *      							 WAIT_FOR_CPUMASK_EMPTY
		 * ALL_LIST_ENTRY_REMOVED
		 * IPI_CANNOT_BE_TRIGGERED
		 * SO_CPUMASK_CANNOT_BE_EMPTY
		 *      							 STILL_WATINIG_FOR_CPUMASK_EMPTY
		 *      							 ...WTF?????????????????
		 *
		 * This is why I implemented 'list_for_each_entry_safe_rcu'.
		 * Seems legit? :P
		 */
		list_for_each_entry_safe_rcu(handler_ptr, handler_nptr, &nmictrl_handler_list, handler_list) {
			if (cpumask_test_and_clear_cpu(smp_processor_id(), &handler_ptr->handler_mask)) {
				nmictrl_fn_t handler_fn;

				if (unlikely((handler_fn = handler_ptr->handler_fn) == NULL))
				{
					break;
				}
				if ((handler_ret = handler_fn(regs)) == NMICTRL_FORWARD) {
					rcu_read_unlock();
					return NMI_DONE;
				}
			}
		}
		rcu_read_unlock();
	}
	return NMI_HANDLED;
}

/**
 * @brief Internal function to reclaim a user-registered handler when rcu grace period expired.
 *
 * It always passes to 'call_rcu' kernel function; do not call this function directly.
 * #handler_rcu will automatically be filled by kernel rcu reclaimer callee.
 */
static void nmictrl_reclaim_handler(struct rcu_head *handler_rcu)
{
	nmictrl_handler_t *handler_ptr =
		container_of(handler_rcu, nmictrl_handler_t, handler_rcu);

	pr_info("Successfully unregistered nmi_handler(%p:%s:%p)\n",
			handler_ptr, handler_ptr->handler_name, handler_ptr->handler_fn);
	handler_ptr->handler_fn = NULL;
	kfree(handler_ptr);
}

/**
 * @brief internal function to flush-out all user-registered handlers.
 */
static void nmictrl_clear_handler_unlocked(void)
{
	nmictrl_handler_t *handler_ptr, *handler_nptr;

	list_for_each_entry_safe(handler_ptr, handler_nptr, &nmictrl_handler_list, handler_list) {
		list_del_rcu(&handler_ptr->handler_list);
		call_rcu(&handler_ptr->handler_rcu, nmictrl_reclaim_handler);
	}
}

int nmictrl_init(void)
{
	int ret;

	spin_lock(&nmictrl_global_write_lock);
	ret = register_nmi_handler(NMI_LOCAL, nmictrl_generic_handler, 0, NMICTRL_GENERIC_HANDLER_NAME);
	wmb();
	spin_unlock(&nmictrl_global_write_lock);
	pr_info("Successfully attached nmictrl-subsystem\n");
	return ret;
}

void nmictrl_shutdown(void)
{
	spin_lock(&nmictrl_global_write_lock);
	nmictrl_clear_handler_unlocked();
	/*
	 * Put memory barrier here to prevent overlapping between cpumask_clear code and new IPI signal.
	 *
	 * All 'nmictrl_trigger_*' functions check handler list existence before they raise IPI signal.
	 * Thus, when all handler list flushed-out, no more IPI signal will generate.
	 *
	 * This memory barrier guaranteeing all handler flushed-out before we clear the cpumask.
	 */
	smp_wmb();
	cpumask_clear(&nmictrl_processor_mask);
	smp_wmb();
	unregister_nmi_handler(NMI_LOCAL, NMICTRL_GENERIC_HANDLER_NAME);
	wmb();
	spin_unlock(&nmictrl_global_write_lock);
	pr_info("Successfully detached nmictrl-subsystem\n");
}

void nmictrl_shutdown_sync(void)
{
	/*
	 * Try to trigger all prepared handlers.
	 */
	nmictrl_trigger_all();
	spin_lock(&nmictrl_global_write_lock);
	nmictrl_clear_handler_unlocked();
	smp_wmb();
	/*
	 * NMI-Enter detecting phase.
	 * If 'nmictrl_processor_mask' is not empty, meaning triggered but not yet handled IPI signal still exist.
	 * An each bit of 'nmictrl_processor_mask' is cleared when IPI signal handled and entered NMI context.
	 * To sum up, for detecting that all IPI signals handled, we must observe the cpumask is empty.
	 */
	while(!cpumask_empty(&nmictrl_processor_mask))
		cpu_relax();
	/*
	 * NMI-Exit detecting phase.
	 * 'nmictrl_generic_handler' always return with rcu_read_unlock.
	 * Therefore, we can determine that other CPUs escaped from NMI context by detecting the RCU grace period.
	 */
	rcu_barrier();
	/*
	 * No unhandled NMI. Hooray!
	 */
	unregister_nmi_handler(NMI_LOCAL, NMICTRL_GENERIC_HANDLER_NAME);
	wmb();
	spin_unlock(&nmictrl_global_write_lock);
	pr_info("Successfully detached nmictrl-subsystem\n");
}

void nmictrl_trigger_all(void)
{
	rcu_read_lock();
	if (!list_empty(&nmictrl_handler_list))
	{
		rcu_read_unlock();
		apic->send_IPI_all(NMI_VECTOR);
		goto skip_unlock;
	}
	rcu_read_unlock();
skip_unlock:;
}

void nmictrl_trigger_self(void)
{
	rcu_read_lock();
	if (!list_empty(&nmictrl_handler_list))
	{
		rcu_read_unlock();
		apic->send_IPI_self(NMI_VECTOR);
		goto skip_unlock;
	}
	rcu_read_unlock();
skip_unlock:;
}

void nmictrl_trigger_others(void)
{
	rcu_read_lock();
	if (!list_empty(&nmictrl_handler_list))
	{
		rcu_read_unlock();
		apic->send_IPI_allbutself(NMI_VECTOR);
		goto skip_unlock;
	}
	rcu_read_unlock();
skip_unlock:;
}

void nmictrl_trigger_cpu(unsigned int cpu_id)
{
	rcu_read_lock();
	if (!list_empty(&nmictrl_handler_list))
	{
		cpumask_t local_mask;

		rcu_read_unlock();
		cpumask_clear(&local_mask);
		cpumask_set_cpu(cpu_id, &local_mask);
		apic->send_IPI_mask(&local_mask, NMI_VECTOR);
		goto skip_unlock;
	}
	rcu_read_unlock();
skip_unlock:;
}

int nmictrl_add_handler(const char *handler_name, nmictrl_fn_t handler_fn)
{
	nmictrl_handler_t *handler_ptr;

	spin_lock(&nmictrl_global_write_lock);
	list_for_each_entry(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			goto error;
		}
	}

	handler_ptr = kzalloc(sizeof(*handler_ptr), GFP_ATOMIC);
	if ( handler_ptr == NULL ) {
		goto error;
	}

	if ( handler_fn == NULL ) {
		goto error;
	}

	if (handler_name == NULL ||
		strlen(handler_name) >= NMICTRL_HANDLER_NAMESZ ||
		strlen(handler_name) !=
			strlcpy(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ)) {
		goto error;
	}
	handler_ptr->handler_fn = handler_fn;
	cpumask_clear(&handler_ptr->handler_mask);

	smp_wmb();
	list_add_rcu(&handler_ptr->handler_list, &nmictrl_handler_list);

	spin_unlock(&nmictrl_global_write_lock);
	pr_info("Successfully registered nmi_handler(%p:%s:%p)\n",
		handler_ptr, handler_ptr->handler_name, handler_ptr->handler_fn);
	return NMICTRL_SUCCESS;

error:
	spin_unlock(&nmictrl_global_write_lock);
	pr_warn("Failed to register nmi_handler(%p:%s->%s:%p)\n",
		handler_ptr, !!(handler_name) ? handler_name : "NULL",
		!!(handler_ptr) ? handler_ptr->handler_name : "NULL", (void *)handler_fn);
	return NMICTRL_ERROR;
}

void nmictrl_del_handler(const char *handler_name)
{
	nmictrl_handler_t *handler_ptr;

	spin_lock(&nmictrl_global_write_lock);
	list_for_each_entry(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			list_del_rcu(&handler_ptr->handler_list);
			smp_wmb();
			call_rcu(&handler_ptr->handler_rcu, nmictrl_reclaim_handler);
		}
	}
	spin_unlock(&nmictrl_global_write_lock);
}

void nmictrl_clear_handler(void)
{
	spin_lock(&nmictrl_global_write_lock);
	nmictrl_clear_handler_unlocked();
	spin_unlock(&nmictrl_global_write_lock);
}

void nmictrl_prepare_handler(const char *handler_name, unsigned int cpu_id)
{
	nmictrl_handler_t *handler_ptr;

	rcu_read_lock();
	list_for_each_entry_rcu(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			if (!cpumask_test_and_set_cpu(cpu_id, &handler_ptr->handler_mask)) {
				smp_wmb();
				cpumask_set_cpu(cpu_id, &nmictrl_processor_mask);
				break;
			}
		}
	}
	rcu_read_unlock();
}