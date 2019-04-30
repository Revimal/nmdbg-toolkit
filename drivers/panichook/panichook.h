#ifndef _NMDBG_PANICHOOK_H
#define _NMDBG_PANICHOOK_H

#include "nmictrl.h"

#define PANICHOOK_HANDLER_NAMESZ 32

/**
 * @brief User-defined handler function type.
 */
typedef void (*panichook_fn_t)(void);

/**
 * @brief Initialize panichook's member variables.
 *
 * panichook_resolve_kfn_symbol() must be not called in a interrupt context.
 * So, we wrote an extra function to lookup kernel symbol addresses.
 */
void panichook_member_init(void);

/**
 * @brief Activate the panichook subsys.
 *
 * This function activates the panichook subsys by attaching to the nmictrl coresys.
 *
 * @param regs
 *	Unused (nmictrl reserve)
 * @return
 *	NMICTRL_HANDLED (always)
 */
nmictrl_ret_t panichook_attach_nmifn(struct pt_regs *regs);

/**
 * @brief Deactivate the panichook subsys.
 *
 * This function deactivates the panichook subsys by detaching from the nmictrl coresys.
 *
 * @param regs
 *	Unused (nmictrl reserve)
 * @return
 *	NMICTRL_HANDLED (always)
 */
nmictrl_ret_t panichook_detach_nmifn(struct pt_regs *regs);

/**
 * @brief Make sure the panichook subsys was successfully activated.
 *
 * @params timeout
 * 	syncing timeout (microsec)
 * @return
 * 	0 if subsys was activated.
 */
int panichook_sync_attach(unsigned long timeout);

/**
 * @brief Make sure the panichook subsys was successfully deactivated.
 *
 * @params timeout
 * 	syncing timeout (microsec)
 * @return
 * 	0 if subsys was deactivated.
 */
int panichook_sync_detach(unsigned long timeout);

#endif