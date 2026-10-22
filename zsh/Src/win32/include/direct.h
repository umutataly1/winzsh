/*
 * direct.h - Win32 extension of the Microsoft C runtime's direct.h
 *
 * The Microsoft C runtime's getcwd() is deprecated in favour of
 * _getcwd(), which has the same interface.  The shell, however, wants
 * the current directory in the slash-separated form that the rest of
 * its path handling assumes, so getcwd() is mapped to the small
 * wrapper in compat.c.
 */

#ifndef _WIN32_ZSH_DIRECT_H
#define _WIN32_ZSH_DIRECT_H

#include_next <direct.h>

#undef getcwd
#define getcwd(buf, size) zsh_win32_getcwd((buf), (size))

char *zsh_win32_getcwd(char *buf, size_t size);

#endif /* _WIN32_ZSH_DIRECT_H */
