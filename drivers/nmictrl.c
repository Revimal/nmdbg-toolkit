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

static DEFINE_SPINLOCK(nmictrl_handler_list_lock);
static LIST_HEAD(nmictrl_handler_list);

/* operate as list_foreach_entry_safe_rcu */
#define list_for_each_entry_nmi_rcu(pos, n, head, member) \
	for (pos = list_entry_rcu((head)->next, typeof(*pos), member), \
		n = list_entry_rcu((pos)->member.next, typeof(*pos), member); \
		 &pos->member != (head); \
		 pos = n, n = list_entry_rcu((pos)->member.next, typeof(*pos), member))

/* RCU protected */
static int nmictrl_generic_handler(unsigned int cmd, struct pt_regs *regs)
{
	nmictrl_ret_t handler_ret;
	nmictrl_handler_t *handler_ptr, *handler_nptr;

	if (cpumask_test_and_clear_cpu(smp_processor_id(), &nmictrl_processor_mask))
	{
		rcu_read_lock();
		list_for_each_entry_nmi_rcu(handler_ptr, handler_nptr, &nmictrl_handler_list, handler_list) {
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
		return NMI_HANDLED;
	}
	return NMI_DONE;
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

int nmictrl_add_handler(const char *handler_name, nmictrl_fn_t handler_fn)
{
	int ret;
	nmictrl_handler_t *handler_ptr;

	spin_lock(&nmictrl_handler_list_lock);
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

	if (list_empty(&nmictrl_handler_list)) {
		ret = register_nmi_handler(NMI_LOCAL, nmictrl_generic_handler, 0, NMICTRL_GENERIC_HANDLER_NAME);
		if (ret)
			goto error;
	}
	list_add_rcu(&handler_ptr->handler_list, &nmictrl_handler_list);
	wmb();

	spin_unlock(&nmictrl_handler_list_lock);
	pr_info("Successfully registered nmi_handler(%p:%s:%p)\n",
		handler_ptr, handler_ptr->handler_name, handler_ptr->handler_fn);
	return ret;

error:
	spin_unlock(&nmictrl_handler_list_lock);
	pr_warn("Failed to register nmi_handler(%p:%s->%s:%p)\n",
		handler_ptr, !!(handler_name) ? handler_name : "NULL",
		!!(handler_ptr) ? handler_ptr->handler_name : "NULL", (void *)handler_fn);
	return ret;
}

void nmictrl_del_handler(const char *handler_name)
{
	nmictrl_handler_t *handler_ptr;

	spin_lock(&nmictrl_handler_list_lock);
	list_for_each_entry(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			list_del_rcu(&handler_ptr->handler_list);
			call_rcu(&handler_ptr->handler_rcu, nmictrl_reclaim_handler);
		}
	}

	if (list_empty(&nmictrl_handler_list)) {
		unregister_nmi_handler(NMI_LOCAL, NMICTRL_GENERIC_HANDLER_NAME);
		wmb();
		while(!cpumask_empty(&nmictrl_processor_mask))
			cpu_relax();
		rcu_barrier();
	}

	spin_unlock(&nmictrl_handler_list_lock);
}

void nmictrl_trigger(const char *handler_name)
{
	nmictrl_handler_t *handler_ptr;

	rcu_read_lock();
	list_for_each_entry_rcu(handler_ptr, &nmictrl_handler_list, handler_list) {
		if (strncmp(handler_ptr->handler_name, handler_name, NMICTRL_HANDLER_NAMESZ) == 0) {
			if (!cpumask_test_and_set_cpu(smp_processor_id(), &handler_ptr->handler_mask)) {
				if (!cpumask_test_cpu(smp_processor_id(), &nmictrl_processor_mask))
				{
					cpumask_t local_mask;

					cpumask_set_cpu(smp_processor_id(), &nmictrl_processor_mask);
					rcu_read_unlock();
					cpumask_clear(&local_mask);
					cpumask_set_cpu(smp_processor_id(), &local_mask);
					apic->send_IPI_mask(&local_mask, NMI_VECTOR);
					goto skip_unlock;
				}
			}
		}
	}
	rcu_read_unlock();
skip_unlock:;
}