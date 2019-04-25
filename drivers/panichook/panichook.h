#ifndef _NMDBG_PANICHOOK_H
#define _NMDBG_PANICHOOK_H

#include "nmictrl.h"

typedef void (*panichook_fn_t)(void);

void panichook_member_init(void);
nmictrl_ret_t panichook_attach_nmifn(struct pt_regs *);
nmictrl_ret_t panichook_detach_nmifn(struct pt_regs *);
int panichook_sync_attach(unsigned long timeout);
int panichook_sync_detach(unsigned long timeout);

#endif