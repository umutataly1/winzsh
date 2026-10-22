/*
 * langinfo.h - locale information for the native Win32 port of zsh
 *
 * Windows has no nl_langinfo(), but the shell only ever asks for the
 * codeset of the current locale, and the answer is always UTF-8: that is
 * what the port writes to the console and to files.
 */

#ifndef _WIN32_ZSH_LANGINFO_H
#define _WIN32_ZSH_LANGINFO_H

#define CODESET 0
#define D_T_FMT 1
#define D_FMT   2
#define T_FMT   3

#ifdef __cplusplus
extern "C" {
#endif

char *zsh_win32_nl_langinfo(int item);

#ifdef __cplusplus
}
#endif

#define nl_langinfo(item) zsh_win32_nl_langinfo(item)

#endif /* _WIN32_ZSH_LANGINFO_H */
