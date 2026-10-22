/*
 * sys/wait.h - process status constants for the native Win32 port
 */
#ifndef _WIN32_ZSH_SYS_WAIT_H
#define _WIN32_ZSH_SYS_WAIT_H

#include <sys/types.h>
#include <sys/resource.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WNOHANG		1
#define WUNTRACED	2
#define WCONTINUED	4
#define WEXITED		8
#define WSTOPPED	16
#define WNOWAIT		32

/* wait status encoding, as used by the traditional Unix interface:
 *   bits 0-6:  signal number (or 0x7f for stopped)
 *   bits 8-15: exit status
 */
#define WIFEXITED(X)	(((X) & 0x7f) == 0)
#define WEXITSTATUS(X)	(((X) >> 8) & 0xff)
#define WIFSIGNALED(X)	((((X) & 0x7f) + 1) >> 1 > 0)
#define WTERMSIG(X)	((X) & 0x7f)
#define WCOREDUMP(X)	((X) & 0x80)
#define WIFSTOPPED(X)	(((X) & 0x7f) == 0x7f)
#define WSTOPSIG(X)	(((X) >> 8) & 0xff)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

/*
 * The traditional interface used a union as the status argument.  zsh's
 * prototypes.h still uses that form, so the union is provided here and
 * wait3() is declared to match.
 */
union wait {
    int w_status;
};

int wait3(union wait *wait_status, int options, struct rusage *rusage);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_SYS_WAIT_H */
