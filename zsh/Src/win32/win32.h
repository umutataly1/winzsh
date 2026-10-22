/*
 * win32.h - common definitions for the native Win32 port of zsh
 *
 * This header pulls in the Win32 API and the few pieces of the Microsoft
 * C runtime that the compat layer needs.  It is deliberately free of any
 * zsh internals so that the compat layer can be compiled as plain C.
 */

#ifndef _WIN32_ZSH_WIN32_H
#define _WIN32_ZSH_WIN32_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#endif

#include <windows.h>
#include <winioctl.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>

/*
 * The Microsoft C runtime's open() does not know about O_BINARY/O_TEXT
 * spelled the POSIX way, and it lacks a couple of the flags zsh uses.
 */
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_TEXT
#define O_TEXT 0
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

/* Handy shorthand for the compiler's warning about unused parameters. */
#define UNUSED_ARG(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* Interface between the compat layer and the zsh specific code	      */
/* ------------------------------------------------------------------ */

/* Path translation: returns a malloc()ed string, or NULL on failure. */
char *zsh_win32_path(const char *path);
/* True for an absolute path in the Windows sense; see compat.c. */
int  zsh_win32_isabs(const char *path);
/* Convert the backslashes in a path to forward slashes, in place. */
void zsh_win32_slashes(char *path);

/* Signal handling (Src/win32/signal.c). */
void zsh_win32_signal_init(void);
int  zsh_win32_poll_signals(void);
int  zsh_win32_signals_pending(void);
void zsh_win32_raise(int sig);
void zsh_win32_raise_default(int sig);
void zsh_win32_wait_for_event(int timeout_ms);
void zsh_win32_check_mailbox(void);
void zsh_win32_register_child(pid_t pid, HANDLE h);
void zsh_win32_unregister_child(pid_t pid);
int  zsh_win32_child_status(DWORD exitcode);

/* Terminal handling (Src/win32/term.c). */
int  zsh_win32_read(int fd, void *buf, unsigned int n);
int  zsh_win32_fd_is_console(int fd);
int  zsh_win32_is_wine(void);
void zsh_win32_init_console(void);

/*
 * Width of the console buffer used under Wine, where the console does not
 * understand the escape sequences the line editor sends and therefore
 * loses track of the cursor position.
 */
#define ZSH_WIN32_WIDE_CONSOLE 4096

#endif /* _WIN32_ZSH_WIN32_H */
