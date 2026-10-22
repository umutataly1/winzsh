/*
 * fnmatch.h - filename matching for the native Win32 port
 */
#ifndef _WIN32_ZSH_FNMATCH_H
#define _WIN32_ZSH_FNMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define FNM_NOMATCH	1
#define FNM_PATHNAME	2
#define FNM_NOESCAPE	4
#define FNM_PERIOD	8
#define FNM_CASEFOLD	16

int fnmatch(const char *pattern, const char *string, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_FNMATCH_H */
