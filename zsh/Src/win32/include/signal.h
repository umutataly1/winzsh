/*
 * signal.h - POSIX style signal interface for the native Win32 port of zsh
 *
 * The Microsoft C runtime only knows about a handful of signals and has no
 * support for sigaction(), sigprocmask() or sigsuspend().  zsh needs all of
 * those, so this header pulls in the CRT definitions and then adds the
 * missing pieces.  The actual implementation lives in Src/win32/signal.c.
 *
 * The signal numbers follow the traditional Unix numbering so that the
 * generated signames.c tables stay readable.  The CRT's own numbers for
 * SIGABRT (22) and SIGBREAK (21) clash with SIGTTOU and SIGTTIN, so
 * SIGABRT is moved back to its traditional value of 6 (which is also the
 * value of SIGIOT and SIGABRT_COMPAT).
 */

#ifndef _WIN32_ZSH_SIGNAL_H
#define _WIN32_ZSH_SIGNAL_H

#include_next <signal.h>

/*
 * The CRT defines a few signals that are not covered below; force the
 * traditional Unix values so that the shell's tables are consistent.
 */
#undef SIGINT
#define SIGINT 2
#undef SIGILL
#define SIGILL 4
#undef SIGFPE
#define SIGFPE 8
#undef SIGSEGV
#define SIGSEGV 11
#undef SIGTERM
#define SIGTERM 15

#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef SIGEMT
#define SIGEMT 7
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGBUS
#define SIGBUS 10
#endif
#ifndef SIGSYS
#define SIGSYS 12
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#ifndef SIGCONT
#define SIGCONT 18
#endif
#ifndef SIGSTOP
#define SIGSTOP 19
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
#ifndef SIGTTIN
#define SIGTTIN 21
#endif
#ifndef SIGTTOU
#define SIGTTOU 22
#endif
#ifndef SIGURG
#define SIGURG 23
#endif
#ifndef SIGXCPU
#define SIGXCPU 24
#endif
#ifndef SIGXFSZ
#define SIGXFSZ 25
#endif
#ifndef SIGVTALRM
#define SIGVTALRM 26
#endif
#ifndef SIGPROF
#define SIGPROF 27
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIGIO
#define SIGIO 29
#endif
#ifndef SIGPOLL
#define SIGPOLL 29
#endif
#ifndef SIGPWR
#define SIGPWR 30
#endif
#ifndef SIGUSR1
#define SIGUSR1 16
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif

/*
 * There are no real-time signals on Windows; leaving SIGRTMIN and
 * SIGRTMAX undefined keeps the shell's signal tables small and simple.
 */

/*
 * The CRT uses 22 for SIGABRT; move it back to the traditional value so
 * that SIGTTOU can have its usual number.
 */
#undef SIGABRT
#define SIGABRT 6

#ifndef SIGIOT
#define SIGIOT 6
#endif

/*
 * POSIX signal API.  sigset_t is a plain bit set of signal numbers; the
 * maximum signal number we support is 63.
 */
#ifndef _WIN32_ZSH_SIGSET_T
#define _WIN32_ZSH_SIGSET_T
typedef unsigned long long sigset_t;
#endif

#define SIG_SETSIZE 64

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP 0x00000001
#define SA_ONSTACK   0x00000002
#define SA_RESTART   0x00000004
#define SA_NODEFER   0x00000008
#define SA_RESETHAND 0x00000010
#define SA_SIGINFO   0x00000020

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_sigaction)(int, void *, void *);
};

/*
 * Use the plain function pointer type so that the values can be compared
 * with the sa_handler member of struct sigaction.
 */
#undef SIG_DFL
#undef SIG_IGN
#define SIG_DFL ((void (*)(int)) 0)
#define SIG_IGN ((void (*)(int)) 1)

#ifndef _WIN32_ZSH_SIGPROTO
#define _WIN32_ZSH_SIGPROTO
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigsuspend(const sigset_t *set);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int sigpending(sigset_t *set);
unsigned int alarm(unsigned int seconds);
int kill(int pid, int sig);
int killpg(int pgrp, int sig);
int getpgrp(void);
int setpgid(int pid, int pgid);
int getpgid(int pid);
void (*zsh_win32_signal(int signo, void (*func)(int)))(int);
#endif

/*
 * Intercept the CRT's signal() so that zsh's signal_ignore() and
 * signal_default() go through our own implementation.
 */
#define signal(signo, func) zsh_win32_signal(signo, func)

#endif /* _WIN32_ZSH_SIGNAL_H */
