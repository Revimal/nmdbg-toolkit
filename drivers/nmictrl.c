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

static cpumask_t nmictrl_processor_mask;

static DEFINE_SPINLOCK(nmictrl_global_write_lock);
static LIST_HEAD(nmictrl_handler_list);

/* RCU protected */
static int nmictrl_generic_handler(unsigned int cmd, struct pt_regs *regs)
{
	nmictrl_ret_t handler_ret;
	nmictrl_handler_t *handler_ptr, *handler_nptr;

	if (cpumask_test_and_clear_cpu(smp_processor_id(), &nmictrl_processor_mask))
	{
		rcu_read_lock();
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

static void nmictrl_reclaim_handler(struct rcu_head *handler_rcu)
{
	nmictrl_handler_t *handler_ptr =
		container_of(handler_rcu, nmictrl_handler_t, handler_rcu);

	pr_info("Successfully unregistered nmi_handler(%p:%s:%p)\n",
			handler_ptr, handler_ptr->handler_name, handler_ptr->handler_fn);
	handler_ptr->handler_fn = NULL;
	kfree(handler_ptr);
}

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
	unsigned long irqflag;

	spin_lock_irqsave(&nmictrl_global_write_lock, irqflag);
	ret = register_nmi_handler(NMI_LOCAL, nmictrl_generic_handler, 0, NMICTRL_GENERIC_HANDLER_NAME);
	wmb();
	spin_unlock_irqrestore(&nmictrl_global_write_lock, irqflag);
	pr_info("Successfully attached nmictrl-subsystem\n");
	return ret;
}

void nmictrl_shutdown(void)
{
	unsigned long irqflag;

	spin_lock_irqsave(&nmictrl_global_write_lock, irqflag);
	nmictrl_clear_handler_unlocked();
	smp_wmb();
	cpumask_clear(&nmictrl_processor_mask);
	smp_wmb();
	unregister_nmi_handler(NMI_LOCAL, NMICTRL_GENERIC_HANDLER_NAME);
	spin_unlock_irqrestore(&nmictrl_global_write_lock, irqflag);
	pr_info("Successfully detached nmictrl-subsystem\n");
}

void nmictrl_shutdown_sync(void)
{
	spin_lock(&nmictrl_global_write_lock);
	nmictrl_clear_handler_unlocked();
	while(!cpumask_empty(&nmictrl_processor_mask))
		cpu_relax();
	rcu_barrier();
	smp_wmb();
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
	int ret;
	nmictrl_handler_t *handler_ptr;

	spin_lock(&nmictrl_global_write_lock);
	list_for_each_entry(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			ret = NMICTRL_ERROR;
			goto error;
		}
	}

	handler_ptr = kzalloc(sizeof(*handler_ptr), GFP_ATOMIC);
	if ( handler_ptr == NULL ) {
		ret = NMICTRL_ERROR;
		goto error;
	}

	if ( handler_fn == NULL ) {
		ret = NMICTRL_ERROR;
		goto error;
	}

	if (handler_name == NULL ||
		strlen(handler_name) >= NMICTRL_HANDLER_NAMESZ ||
		strlen(handler_name) !=
			strlcpy(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ)) {
		ret = NMICTRL_ERROR;
		goto error;
	}
	handler_ptr->handler_fn = handler_fn;
	cpumask_clear(&handler_ptr->handler_mask);

	smp_wmb();
	list_add_rcu(&handler_ptr->handler_list, &nmictrl_handler_list);

	spin_unlock(&nmictrl_global_write_lock);
	pr_info("Successfully registered nmi_handler(%p:%s:%p)\n",
		handler_ptr, handler_ptr->handler_name, handler_ptr->handler_fn);
	return ret;

error:
	spin_unlock(&nmictrl_global_write_lock);
	pr_warn("Failed to register nmi_handler(%p:%s->%s:%p)\n",
		handler_ptr, !!(handler_name) ? handler_name : "NULL",
		!!(handler_ptr) ? handler_ptr->handler_name : "NULL", (void *)handler_fn);
	return ret;
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