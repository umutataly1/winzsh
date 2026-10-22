/** signames.c                                 **/
/** architecture-customized signames.c for zsh **/

#define SIGCOUNT	31

#include "zsh.mdh"

/**/
mod_export char *sig_msg[SIGCOUNT+2] = {
	"done",
	"hangup",
	"interrupt",
	"quit",
	"illegal hardware instruction",
	"trace trap",
	"abort",
	"EMT instruction",
	"floating point exception",
	"killed",
	"bus error",
	"segmentation fault",
	"invalid system call",
	"broken pipe",
	"alarm",
	"terminated",
	"user-defined signal 1",
	"death of child",
	"continued",
# ifdef USE_SUSPENDED
	"suspended (signal)",
# else
	"stopped (signal)",
# endif
# ifdef USE_SUSPENDED
	"suspended (signal)",
# else
	"stopped (signal)",
# endif
	"SIGTTIN",
	"SIGTTOU",
	"SIGURG",
	"SIGXCPU",
	"SIGXFSZ",
	"SIGVTALRM",
	"SIGPROF",
	"window size changed",
	"SIGIO",
	"SIGPWR",
	"user-defined signal 2",
	NULL
};

/**/
char *sigs[SIGCOUNT+4] = {
	"EXIT",
	"HUP",
	"INT",
	"QUIT",
	"ILL",
	"TRAP",
	"ABRT",
	"EMT",
	"FPE",
	"KILL",
	"BUS",
	"SEGV",
	"SYS",
	"PIPE",
	"ALRM",
	"TERM",
	"USR1",
	"CHLD",
	"CONT",
	"STOP",
	"TSTP",
	"TTIN",
	"TTOU",
	"URG",
	"XCPU",
	"XFSZ",
	"VTALRM",
	"PROF",
	"WINCH",
	"IO",
	"PWR",
	"USR2",
	"ZERR",
	"DEBUG",
	NULL
};
