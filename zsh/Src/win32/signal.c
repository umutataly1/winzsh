/*
 * signal.c - signal handling for the native Win32 port of zsh
 *
 * Windows has no POSIX signals.  This file implements just enough of the
 * traditional Unix signal model for zsh to work:
 *
 *   - signals are queued in a bit mask and delivered at "safe" points,
 *   - the signal mask can be blocked and suspended on,
 *   - ^C is turned into SIGINT by a console control handler,
 *   - the death of a child produces SIGCHLD,
 *   - signals can be sent to another zsh process through a small shared
 *     memory mailbox.
 */

#include "win32.h"

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <process.h>

#define ZSH_NSIG 64

/* The handler installed for a signal, or SIG_DFL / SIG_IGN. */
static void (*zsh_handlers[ZSH_NSIG])(int);
/* Signals that have been raised but not yet delivered. */
static volatile unsigned long long zsh_pending;
/* Signals that are currently blocked. */
static volatile unsigned long long zsh_blocked;
/* Set while a handler is running, to avoid recursive delivery. */
static volatile int zsh_in_handler;

/* The console control handler wakes us up through this event. */
static HANDLE zsh_sig_event;
/* Mailbox used to receive signals from other processes. */
static HANDLE zsh_sig_map_handle;
static volatile unsigned long long *zsh_sig_map;
/* The named event other processes set to tell us a signal is waiting. */
static HANDLE zsh_sig_named_event;

/* ------------------------------------------------------------------ */
/* Child processes						      */
/* ------------------------------------------------------------------ */

struct zsh_child {
    pid_t pid;
    HANDLE h;
    /* The signal mailbox of the child, created on its behalf. */
    HANDLE sig_map;
    HANDLE sig_event;
    DWORD exitcode;
    int exited;			/* the process has terminated */
    int reaped;		/* the status has been handed out */
    int pgid;
    int used;
};

#define ZSH_MAXCHILD 256

static struct zsh_child zsh_children[ZSH_MAXCHILD];

/**/
static struct zsh_child *
zsh_find_child(pid_t pid)
{
    int i;

    for (i = 0; i < ZSH_MAXCHILD; i++)
	if (zsh_children[i].used && zsh_children[i].pid == pid)
	    return &zsh_children[i];
    return NULL;
}

/**/
void
zsh_win32_register_child(pid_t pid, HANDLE h)
{
    int i, freep = -1;

    for (i = 0; i < ZSH_MAXCHILD; i++) {
	if (!zsh_children[i].used) {
	    freep = i;
	    break;
	}
	/*
	 * Reuse the slot of a child that has already been reaped; its
	 * status has been handed out and nobody is interested in it any
	 * more.
	 */
	if (freep < 0 && zsh_children[i].reaped) {
	    freep = i;
	    break;
	}
    }
    if (freep < 0)
	return;
    memset(&zsh_children[freep], 0, sizeof(zsh_children[freep]));
    zsh_children[freep].used = 1;
    zsh_children[freep].pid = pid;
    zsh_children[freep].h = h;
    zsh_children[freep].pgid = pid;

    /*
     * Create the child's signal mailbox here, before it has had time to
     * start up.  Otherwise a signal sent straight after the fork would
     * find no mailbox and be reported as an unknown process.
     */
    {
	wchar_t name[64];

	swprintf(name, 64, L"Local\\zsh-win32-sig-%lu",
		 (unsigned long) pid);
	zsh_children[freep].sig_map =
	    CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
			       sizeof(unsigned long long), name);
	swprintf(name, 64, L"Local\\zsh-win32-sigev-%lu",
		 (unsigned long) pid);
	zsh_children[freep].sig_event =
	    CreateEventW(NULL, FALSE, FALSE, name);
    }
}

/**/
void
zsh_win32_unregister_child(pid_t pid)
{
    struct zsh_child *c = zsh_find_child(pid);

    if (c) {
	if (c->h && c->h != INVALID_HANDLE_VALUE)
	    CloseHandle(c->h);
	if (c->sig_map)
	    CloseHandle(c->sig_map);
	if (c->sig_event)
	    CloseHandle(c->sig_event);
	memset(c, 0, sizeof(*c));
    }
}

/*
 * Look for a child that has terminated and whose status has not been
 * collected yet.  pid == -1 means "any child".
 */
static pid_t
zsh_scan_children(pid_t pid, int *status)
{
    int i;

    for (i = 0; i < ZSH_MAXCHILD; i++) {
	struct zsh_child *c = &zsh_children[i];

	if (!c->used || c->reaped)
	    continue;
	if (pid > 0 && c->pid != pid)
	    continue;
	if (!c->exited) {
	    DWORD code;
	    if (c->h && c->h != INVALID_HANDLE_VALUE &&
		WaitForSingleObject(c->h, 0) == WAIT_OBJECT_0) {
		if (GetExitCodeProcess(c->h, &code))
		    c->exitcode = code;
		c->exited = 1;
	    } else
		continue;
	}
	if (status)
	    *status = zsh_win32_child_status(c->exitcode);
	c->reaped = 1;
	return c->pid;
    }
    return 0;
}

/*
 * Turn a Windows exit code into a Unix style wait status.  A process
 * that was killed by a signal exits with the code 0x10000 | signal.
 */
int
zsh_win32_child_status(DWORD exitcode)
{
    if ((exitcode & 0xffffff00UL) == 0x10000UL)
	return (int) (exitcode & 0x7f);
    return (int) ((exitcode & 0xff) << 8);
}

/*
 * Notice children that have terminated and raise SIGCHLD for them.
 */
static void
zsh_check_children(void)
{
    int i, any = 0;

    for (i = 0; i < ZSH_MAXCHILD; i++) {
	struct zsh_child *c = &zsh_children[i];
	DWORD code;

	if (!c->used || c->exited || !c->h || c->h == INVALID_HANDLE_VALUE)
	    continue;
	if (WaitForSingleObject(c->h, 0) == WAIT_OBJECT_0) {
	    if (GetExitCodeProcess(c->h, &code))
		c->exitcode = code;
	    c->exited = 1;
	    any = 1;
	}
    }
    if (any)
	zsh_win32_raise(SIGCHLD);
}

/* ------------------------------------------------------------------ */
/* Delivery							      */
/* ------------------------------------------------------------------ */

/**/
static void
zsh_deliver(int sig)
{
    void (*fn)(int);

    if (sig <= 0 || sig >= ZSH_NSIG)
	return;
    fn = zsh_handlers[sig];
    if (fn == SIG_IGN)
	return;
    if (fn == NULL || fn == SIG_DFL) {
	switch (sig) {
	case SIGCHLD:
	case SIGURG:
	    return;
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
	case SIGHUP:
	case SIGABRT:
	case SIGBREAK:
	    /* Default action: terminate. */
	    zsh_win32_raise_default(sig);
	    return;
	default:
	    return;
	}
    }
    zsh_in_handler = 1;
    (*fn)(sig);
    zsh_in_handler = 0;
}

/**/
int
zsh_win32_poll_signals(void)
{
    int delivered = 0;

    if (zsh_in_handler)
	return 0;
    zsh_win32_check_mailbox();
    for (;;) {
	unsigned long long pend = zsh_pending & ~zsh_blocked;
	int sig;

	if (!pend)
	    break;
	for (sig = 1; sig < ZSH_NSIG; sig++)
	    if (pend & (1ULL << sig))
		break;
	if (sig >= ZSH_NSIG)
	    break;
	zsh_pending &= ~(1ULL << sig);
	zsh_deliver(sig);
	delivered++;
	if (zsh_in_handler)
	    break;
    }
    return delivered;
}

/**/
int
zsh_win32_signals_pending(void)
{
    return (zsh_pending & ~zsh_blocked) != 0;
}

/**/
void
zsh_win32_raise(int sig)
{
    if (sig <= 0 || sig >= ZSH_NSIG)
	return;
    zsh_pending |= 1ULL << sig;
    if (zsh_sig_event)
	SetEvent(zsh_sig_event);
}

/*
 * Perform the default action for a signal, which for all the signals we
 * care about means terminating the process.  The exit code records the
 * signal so that the parent can tell that the child was killed.
 */
void
zsh_win32_raise_default(int sig)
{
    ExitProcess(0x10000 | (sig & 0xff));
}

/*
 * Wait for something to happen: a child terminating, a console control
 * event, or a signal arriving from another process.
 */
void
zsh_win32_wait_for_event(int timeout_ms)
{
    HANDLE handles[65];
    DWORD n = 0, r;
    int i;

    if (zsh_sig_event)
	handles[n++] = zsh_sig_event;
    if (zsh_sig_named_event)
	handles[n++] = zsh_sig_named_event;
    for (i = 0; i < ZSH_MAXCHILD && n < 64; i++)
	if (zsh_children[i].used && zsh_children[i].h &&
	    zsh_children[i].h != INVALID_HANDLE_VALUE)
	    handles[n++] = zsh_children[i].h;

    if (n == 0) {
	Sleep(timeout_ms > 0 ? (DWORD) timeout_ms : 50);
	return;
    }
    r = WaitForMultipleObjects(n, handles, FALSE,
				timeout_ms > 0 ? (DWORD) timeout_ms : INFINITE);
    if (r == WAIT_FAILED)
	Sleep(50);
}

/* ------------------------------------------------------------------ */
/* POSIX signal API						      */
/* ------------------------------------------------------------------ */

/**/
int
sigemptyset(sigset_t *set)
{
    *set = 0;
    return 0;
}

/**/
int
sigfillset(sigset_t *set)
{
    *set = ~(sigset_t) 0;
    return 0;
}

/**/
int
sigaddset(sigset_t *set, int signo)
{
    if (signo <= 0 || signo >= ZSH_NSIG) {
	errno = EINVAL;
	return -1;
    }
    *set |= 1ULL << signo;
    return 0;
}

/**/
int
sigdelset(sigset_t *set, int signo)
{
    if (signo <= 0 || signo >= ZSH_NSIG) {
	errno = EINVAL;
	return -1;
    }
    *set &= ~(1ULL << signo);
    return 0;
}

/**/
int
sigismember(const sigset_t *set, int signo)
{
    if (signo <= 0 || signo >= ZSH_NSIG) {
	errno = EINVAL;
	return -1;
    }
    return (*set & (1ULL << signo)) ? 1 : 0;
}

/**/
int
sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    sigset_t old = zsh_blocked;

    if (oldset)
	*oldset = old;
    switch (how) {
    case SIG_BLOCK:
	if (set)
	    zsh_blocked |= *set;
	break;
    case SIG_UNBLOCK:
	if (set)
	    zsh_blocked &= ~(*set);
	break;
    case SIG_SETMASK:
	if (set)
	    zsh_blocked = *set;
	break;
    default:
	errno = EINVAL;
	return -1;
    }
    return 0;
}

/**/
int
sigpending(sigset_t *set)
{
    if (set)
	*set = zsh_pending;
    return 0;
}

/**/
int
sigaction(int signo, const struct sigaction *act, struct sigaction *oldact)
{
    if (signo <= 0 || signo >= ZSH_NSIG) {
	errno = EINVAL;
	return -1;
    }
    if (oldact) {
	oldact->sa_handler = zsh_handlers[signo] ? zsh_handlers[signo] : SIG_DFL;
	oldact->sa_mask = 0;
	oldact->sa_flags = 0;
	oldact->sa_sigaction = NULL;
    }
    if (act)
	zsh_handlers[signo] = act->sa_handler;
    return 0;
}

/*
 * The traditional signal() interface, with BSD semantics: the handler is
 * reset to SIG_DFL once it has been called.
 */
void (*zsh_win32_signal(int signo, void (*func)(int)))(int)
{
    void (*old)(int);

    if (signo <= 0 || signo >= ZSH_NSIG) {
	errno = EINVAL;
	return SIG_DFL;
    }
    old = zsh_handlers[signo] ? zsh_handlers[signo] : SIG_DFL;
    zsh_handlers[signo] = func;
    return old;
}

/**/
int
sigsuspend(const sigset_t *set)
{
    sigset_t old = zsh_blocked;

    if (set)
	zsh_blocked = *set;
    for (;;) {
	zsh_check_children();
	if (zsh_win32_poll_signals())
	    break;
	zsh_win32_wait_for_event(100);
    }
    zsh_blocked = old;
    errno = EINTR;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Sending signals						      */
/* ------------------------------------------------------------------ */

/*
 * Deliver a signal to another zsh process using the shared mailbox.
 */
static int
zsh_send_to_process(DWORD pid, int sig)
{
    wchar_t name[64];
    HANDLE map, ev;
    volatile unsigned long long *p;

    swprintf(name, 64, L"Local\\zsh-win32-sig-%lu", (unsigned long) pid);
    map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!map)
	return -1;
    p = (volatile unsigned long long *) MapViewOfFile(map, FILE_MAP_ALL_ACCESS,
						   0, 0, sizeof(*p));
    if (!p) {
	CloseHandle(map);
	return -1;
    }
    *p |= 1ULL << sig;
    UnmapViewOfFile(p);
    CloseHandle(map);

    swprintf(name, 64, L"Local\\zsh-win32-sigev-%lu", (unsigned long) pid);
    ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (ev) {
	SetEvent(ev);
	CloseHandle(ev);
    }
    return 0;
}

/**/
int
kill(pid_t pid, int sig)
{
    struct zsh_child *c;

    if (sig < 0 || sig >= ZSH_NSIG) {
	errno = EINVAL;
	return -1;
    }
    if (pid == 0)
	pid = getpid();
    if (pid == getpid()) {
	if (sig == SIGKILL) {
	    zsh_win32_raise_default(SIGKILL);
	    return 0;
	}
	zsh_win32_raise(sig);
	return 0;
    }
    c = zsh_find_child(pid);
    if (!c || c->reaped) {
	errno = ESRCH;
	return -1;
    }
    if (sig == 0) {
	/*
	 * This is only an existence check, as in POSIX.  A child that
	 * has terminated but has not been reaped yet still exists.
	 */
	return 0;
    }
    if (sig == SIGKILL) {
	/*
	 * The child would never notice a message in its mailbox, so kill
	 * it outright and record the signal in the exit code.
	 */
	TerminateProcess(c->h, 0x10000 | (SIGKILL & 0xff));
	return 0;
    }
    if (zsh_send_to_process((DWORD) pid, sig) < 0) {
	errno = ESRCH;
	return -1;
    }
    return 0;
}

/**/
int
killpg(int pgrp, int sig)
{
    int i, found = 0;

    if (pgrp <= 0)
	pgrp = getpid();
    for (i = 0; i < ZSH_MAXCHILD; i++) {
	struct zsh_child *c = &zsh_children[i];

	if (!c->used || c->reaped || c->pgid != pgrp)
	    continue;
	found = 1;
	if (sig == SIGKILL)
	    TerminateProcess(c->h, 0x10000 | (SIGKILL & 0xff));
	else
	    zsh_send_to_process((DWORD) c->pid, sig);
    }
    if (!found) {
	errno = ESRCH;
	return -1;
    }
    return 0;
}

/**/
int
getpgrp(void)
{
    return (int) getpid();
}

/**/
int
setpgid(pid_t pid, int pgid)
{
    struct zsh_child *c;

    if (pid == 0)
	pid = getpid();
    if (pid == getpid())
	return 0;
    c = zsh_find_child(pid);
    if (!c) {
	errno = ESRCH;
	return -1;
    }
    c->pgid = pgid;
    return 0;
}

/**/
int
getpgid(pid_t pid)
{
    struct zsh_child *c;

    if (pid == 0)
	pid = getpid();
    if (pid == getpid())
	return (int) getpid();
    c = zsh_find_child(pid);
    if (!c) {
	errno = ESRCH;
	return -1;
    }
    return c->pgid;
}

/* ------------------------------------------------------------------ */
/* Waiting for children						      */
/* ------------------------------------------------------------------ */

/**/
pid_t
waitpid(pid_t pid, int *status, int options)
{
    pid_t r;

    if (pid > 0) {
	struct zsh_child *c = zsh_find_child(pid);

	if (!c || c->reaped) {
	    /* There is no such child (any more). */
	    errno = ECHILD;
	    return -1;
	}
    }
    for (;;) {
	r = zsh_scan_children(pid, status);
	if (r)
	    return r;
	if (options & WNOHANG)
	    return 0;
	zsh_check_children();
	if (zsh_win32_poll_signals()) {
	    errno = EINTR;
	    return -1;
	}
	zsh_win32_wait_for_event(100);
    }
}

/**/
pid_t
wait(int *status)
{
    return waitpid(-1, status, 0);
}

/**/
int
wait3(union wait *wait_status, int options, struct rusage *rusage)
{
    pid_t r = waitpid(-1, wait_status ? &wait_status->w_status : NULL,
		      options);

    if (rusage)
	getrusage(RUSAGE_CHILDREN, rusage);
    return r;
}

/* ------------------------------------------------------------------ */
/* Console control handling					      */
/* ------------------------------------------------------------------ */

static BOOL WINAPI
zsh_console_ctrl(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
	zsh_win32_raise(SIGINT);
	return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
	zsh_win32_raise(SIGHUP);
	return TRUE;
    default:
	return FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* alarm()							      */
/* ------------------------------------------------------------------ */

static HANDLE zsh_alarm_event;
static volatile DWORD zsh_alarm_seconds;

static DWORD WINAPI
zsh_alarm_thread(LPVOID unused)
{
    UNUSED_ARG(unused);

    for (;;) {
	DWORD secs = zsh_alarm_seconds;

	if (secs == 0) {
	    if (WaitForSingleObject(zsh_alarm_event, 200) == WAIT_OBJECT_0)
		ResetEvent(zsh_alarm_event);
	    continue;
	}
	if (WaitForSingleObject(zsh_alarm_event, secs * 1000) == WAIT_OBJECT_0) {
	    ResetEvent(zsh_alarm_event);
	    continue;
	}
	zsh_alarm_seconds = 0;
	zsh_win32_raise(SIGALRM);
    }
    return 0;
}

/**/
unsigned int
alarm(unsigned int seconds)
{
    unsigned int old = zsh_alarm_seconds;

    zsh_alarm_seconds = seconds;
    if (!zsh_alarm_event) {
	zsh_alarm_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (zsh_alarm_event)
	    CreateThread(NULL, 0, zsh_alarm_thread, NULL, 0, NULL);
    } else
	SetEvent(zsh_alarm_event);
    return old;
}

/* ------------------------------------------------------------------ */
/* Start-up							      */
/* ------------------------------------------------------------------ */

/**/
void
zsh_win32_signal_init(void)
{
    wchar_t name[64];
    DWORD pid = GetCurrentProcessId();

    memset(zsh_handlers, 0, sizeof(zsh_handlers));
    zsh_pending = 0;
    zsh_blocked = 0;

    zsh_sig_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    swprintf(name, 64, L"Local\\zsh-win32-sig-%lu", (unsigned long) pid);
    zsh_sig_map_handle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
					    PAGE_READWRITE, 0,
					    sizeof(unsigned long long), name);
    if (zsh_sig_map_handle) {
	/*
	 * The mapping is new or was made for us by the parent that forked
	 * us, and in both cases it is already zero.  Do not clear it here:
	 * a signal sent between the fork and this point would be lost.
	 */
	zsh_sig_map = (volatile unsigned long long *)
	    MapViewOfFile(zsh_sig_map_handle, FILE_MAP_ALL_ACCESS, 0, 0,
			  sizeof(unsigned long long));
    }
    swprintf(name, 64, L"Local\\zsh-win32-sigev-%lu", (unsigned long) pid);
    /*
     * The named event must stay open for as long as the process lives:
     * closing the last handle would destroy the object and other
     * processes would no longer be able to signal us.
     */
    zsh_sig_named_event = CreateEventW(NULL, FALSE, FALSE, name);

    SetConsoleCtrlHandler(zsh_console_ctrl, TRUE);
}

/*
 * Pick up signals that arrived through the mailbox.
 */
void
zsh_win32_check_mailbox(void)
{
    if (zsh_sig_map) {
	unsigned long long pend = *zsh_sig_map;

	if (pend) {
	    *zsh_sig_map = 0;
	    zsh_pending |= pend;
	}
    }
}
