/**
 * @file nmictrl.h
 * @brief Prototypes for 'NMI control subsystem'.
 *
 * This contains the function prototypes, macros,
 * structures, enums, etc. for 'NMI control subsystem'
 *
 * @author Hyeonho Seo (Revimal)
 * @bug No Known Bugs
 */

#ifndef _NMDBG_NMICTRL_H
#define _NMDBG_NMICTRL_H

#include <linux/cpumask.h>
#include <asm/ptrace.h>

#include "define.h"

#define NMICTRL_HANDLER_NAMESZ 32
#define NMICTRL_SUCCESS NMICTRL_HANDLED

/**
 * @brief Return values for NMI control subsystem.
 */
typedef enum {
	NMICTRL_HANDLED,
	NMICTRL_ERROR,
	NMICTRL_FORWARD,
} nmictrl_ret_t;

/**
 * @brief User-defined handler function type.
 */
typedef nmictrl_ret_t (*nmictrl_fn_t)(struct pt_regs *);

/**
 * @brief Activate the NMI control system.
 * @return
 * 	0 if initialization success.
 */
int nmictrl_startup(void);

/**
 * @brief deactivate the NMI control system.
 */
void nmictrl_shutdown(void);

/**
 * @brief Deactivate NMI control subsystem and wait for triggered IPI signals to finish.
 */
void nmictrl_shutdown_sync(void);

/**
 * @brief Send IPI signals to entire CPUs.
 */
void nmictrl_trigger_all(void);

/**
 * @brief Send IPI signals to the current CPU.
 */
void nmictrl_trigger_self(void);

/**
 * @brief Send IPI signals to all the other CPUs.
 */
void nmictrl_trigger_others(void);

/**
 * @brief Send IPI signals to the specific CPUs.
 */
void nmictrl_trigger_cpu(unsigned int cpu_id);

/**
 * @brief Register an user-defined handler.
 *
 * Length of @p handler_name must be shorter than 32 characters.
 *
 * @param handler_name
 * 	The handler name to be registered
 * @param handler_fn
 * 	The handler callback to be registered
 * @return
 * 	0 upon successful add the handler
 */
int nmictrl_add_handler(const char *handler_name, nmictrl_fn_t nmi_handler);

/**
 * @brief Unregister an user-defined handler.
 * @param handler_name
 * 	The handler name to be unregistered
 */
void nmictrl_del_handler(const char *handler_name);

/**
 * @brief Unregister all user-defined handlers.
 */
void nmictrl_clear_handler(void);

/**
 * @brief Prepare an user-defined handler.
 *
 * "prepare" means notify to the nmictrl_generic_handler() that there are some handlers must be executed in.
 * The generic handler instantly returns without any actions if no any "prepared" user-handlers.
 *
 * @param handler_name
 * 	The handler name to be prepared
 * @param cpu_id
 * 	The cpu id that handler will be triggered on
 */
void nmictrl_prepare_handler(const char *handler_name, unsigned int cpu_id);
#endif