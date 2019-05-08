/**
 * @file define.h
 * @brief Prototypes for common uses
 *
 * This contains the function prototypes, macros,
 * structures, enums, etc. for common uses.
 *
 * @author Hyeonho Seo (Revimal)
 * @bug No Known Bugs
 */

#ifndef _NMDBG_DEFINE_H
#define _NMDBG_DEFINE_H

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define NMDBG_MODULE_VER "1.0.0"
#ifndef NMDBG_MODULE_MVER
#define NMDBG_MODULE_MVER "unknown"
#endif
#define NMDBG_MODULE_NAME "nmdbg"
#define NMDBG_MODULE_DESC "NMI based kernel debugging toolkit"
#define NMDBG_MODULE_DATE "2018-2019"
#define NMDBG_MODULE_AUTHOR "Hyeonho Seo"
#define NMDBG_MODULE_AUTHINFO "<GPG:A70F1DED>"
#define NMDBG_MODULE_LICENSE "Dual MIT/GPL"

#endif