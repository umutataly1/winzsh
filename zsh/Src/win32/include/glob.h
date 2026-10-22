/*
 * glob.h - pathname expansion for the native Win32 port
 */
#ifndef _WIN32_ZSH_GLOB_H
#define _WIN32_ZSH_GLOB_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLOB_NOMATCH	1
#define GLOB_NOSPACE	2
#define GLOB_ABORTED	3
#define GLOB_NOSORT	4

#define GLOB_MARK	0x01
#define GLOB_NOCHECK	0x02
#define GLOB_BRACE	0x04
#define GLOB_ONLYDIR	0x08
#define GLOB_QUOTE	0x10
#define GLOB_TILDE	0x20
#define GLOB_ALPHASORT	0x40
#define GLOB_APPEND	0x80

typedef struct glob_t {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
    int gl_flags;
    int gl_error;
} glob_t;

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
	glob_t *pglob);
void globfree(glob_t *pglob);
int glob_pattern_p(const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_GLOB_H */
