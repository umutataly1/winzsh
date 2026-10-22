/*
 * fcntl.h - file control for the native Win32 port of zsh
 *
 * The Microsoft C runtime has no fcntl() and knows only a handful of the
 * O_* flags zsh uses.  This header pulls in the CRT header, adds the
 * missing pieces and redirects open()/fcntl() to the implementations in
 * Src/win32/compat.c.
 *
 * Note that <io.h> is included first on purpose: it declares open(),
 * read() and friends, and including it here means that any later
 * inclusion is a no-op, so the macros installed below can never mangle
 * a system declaration.
 */

#ifndef _WIN32_ZSH_FCNTL_H
#define _WIN32_ZSH_FCNTL_H

#include <io.h>
#include_next <fcntl.h>

#include <sys/types.h>

/* Flags the Microsoft C runtime does not know about. */
#ifndef O_NOCTTY
# define O_NOCTTY 0
#endif
#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif
#ifndef O_DIRECTORY
# define O_DIRECTORY 0
#endif
#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif
#ifndef O_NONBLOCK
# define O_NONBLOCK 0
#endif
#ifndef O_NDELAY
# define O_NDELAY 0
#endif
#ifndef O_SYNC
# define O_SYNC 0
#endif

/* fcntl() commands. */
#define F_DUPFD		0
#define F_GETFD		1
#define F_SETFD		2
#define F_GETFL		3
#define F_SETFL		4
#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7

/* Record locks. */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* File descriptor flags. */
#ifndef FD_CLOEXEC
# define FD_CLOEXEC 1
#endif

#ifndef _WIN32_ZSH_STRUCT_FLOCK
#define _WIN32_ZSH_STRUCT_FLOCK
struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

int zsh_win32_fcntl(int fd, int cmd, ...);
int zsh_win32_open(const char *path, int flags, ...);

#ifdef __cplusplus
}
#endif

/*
 * Redirect the calls.  open() is variadic because the mode argument is
 * only present when O_CREAT is given.
 */
#define fcntl(fd, cmd, ...) zsh_win32_fcntl(fd, cmd, ##__VA_ARGS__)
#define open(path, flags, ...) zsh_win32_open(path, flags, ##__VA_ARGS__)

#endif /* _WIN32_ZSH_FCNTL_H */
