/*
 * sys/times.h - process times for the native Win32 port of zsh
 *
 * The Microsoft C runtime has no sys/times.h.  The implementation of
 * times() lives in Src/win32/compat.c and is built on top of
 * GetProcessTimes().
 */

#ifndef _WIN32_ZSH_SYS_TIMES_H
#define _WIN32_ZSH_SYS_TIMES_H

#include <time.h>

#ifndef _WIN32_ZSH_STRUCT_TMS
#define _WIN32_ZSH_STRUCT_TMS
struct tms {
    clock_t tms_utime;       /* user time */
    clock_t tms_stime;       /* system time */
    clock_t tms_cutime;      /* user time of waited-for children */
    clock_t tms_cstime;      /* system time of waited-for children */
};
#endif

#ifndef _WIN32_ZSH_TIMES_DECL
#define _WIN32_ZSH_TIMES_DECL
clock_t times(struct tms *buf);
#endif

#endif /* _WIN32_ZSH_SYS_TIMES_H */
