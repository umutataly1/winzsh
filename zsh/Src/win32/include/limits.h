/*
 * limits.h - limits for the native Win32 port of zsh
 *
 * The Microsoft C runtime's limits.h limits PATH_MAX to the length of
 * a path that was understood by Windows 95.  Windows accepts much
 * longer paths, so a more generous value is used here.
 */

#ifndef _WIN32_ZSH_LIMITS_H
#define _WIN32_ZSH_LIMITS_H

#include_next <limits.h>

#undef PATH_MAX
#define PATH_MAX 4096

#endif /* _WIN32_ZSH_LIMITS_H */
