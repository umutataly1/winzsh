/*
 * zshwin32.h - interface to the Windows specific parts of the shell
 *
 * This header is included by the zsh sources that need to know about
 * the native Windows port.  It deliberately does not pull in the Win32
 * API so that it can be included from ordinary zsh translation units.
 */

#ifndef _WIN32_ZSH_ZSHWIN32_H
#define _WIN32_ZSH_ZSHWIN32_H

#include <sys/types.h>

/*
 * The way this process was started:
 *
 *  ZSH_WIN32_CHILD_NONE	an ordinary shell
 *  ZSH_WIN32_CHILD_SUBSHELL	a subshell started by the fork emulation
 *  ZSH_WIN32_CHILD_MULTIO	 the helper that copies data for the
 *			 multiple redirection feature
 */
#define ZSH_WIN32_CHILD_NONE	0
#define ZSH_WIN32_CHILD_SUBSHELL	1
#define ZSH_WIN32_CHILD_MULTIO	2

extern int zsh_win32_child_mode;

/*
 * Path handling.
 *
 * The shell assumes that an absolute path starts with a slash.  On
 * Windows that is not true: "C:/foo" and "//server/share" are absolute
 * as well, so the shell sources use ZSH_ISABSPATH() where they used to
 * test for a leading slash.  zsh_win32_isabs() lives in compat.c.
 */
int zsh_win32_isabs(const char *path);
void zsh_win32_slashes(char *path);

#ifdef _WIN32
# define ZSH_ISABSPATH(p) zsh_win32_isabs(p)
#else
# define ZSH_ISABSPATH(p) (*(p) == '/')
#endif

/*
 * The arguments of the multiple redirection helper, set when this
 * process was started as that helper.
 */
extern char **zsh_win32_multio_argv;

/*
 * Non-NULL when this process is a subshell started by the fork
 * emulation; they then hold the names of the files the parent left
 * behind for us.
 */
extern char *zsh_win32_child_statefile;
extern char *zsh_win32_child_cmdfile;

/*
 * Start a subshell that runs the command text "cmdtext".
 *
 * "infd" and "outfd" are the file descriptors the child should use for
 * its standard input and output, or -1 to inherit ours.  Returns the
 * process id of the child, or -1 on failure.
 */
pid_t zsh_win32_fork(const char *cmdtext, int infd, int outfd);

/*
 * Replace this process with the program "path".  Like execve(2), but
 * the command line is quoted properly for a Windows child process.
 * Does not return on success; returns -1 with errno set on failure.
 */
int zsh_win32_execve(const char *path, char *const argv[],
		     char *const envp[]);

/*
 * Complete a command name that has no extension with an extension from
 * PATHEXT, as the Windows command processor does.  Returns 1 and fills
 * "buf" (of size "len") on success.
 */
int zsh_win32_appext(const char *name, char *buf, size_t len);

/*
 * Start the helper process that copies data for the multiple
 * redirection feature.  "rflag" is non-zero for the "tee" case, where
 * the data is read from "pipefd" and written to all of "fds"; otherwise
 * the data is read from all of "fds" and written to "pipefd".
 */
pid_t zsh_win32_multio(int rflag, int pipefd, int *fds, int nfds);

/*
 * Look for the magic argument that marks a helper process.  Returns the
 * mode of this process (one of the ZSH_WIN32_CHILD_* values above); if
 * the argument vector has been stripped of the magic words when the
 * process is a subshell.
 */
int zsh_win32_child_start(char **argv);

/*
 * Restore the state left behind by the parent and run the command we
 * were given.  Does not return.
 */
void zsh_win32_child_run(void);

/*
 * Do the copying for the multiple redirection helper.  Does not return.
 */
void zsh_win32_multio_run(void);

/*
 * Initialise the parts of the port that need to run once per process:
 * the signal table and the state of the standard streams.
 */
void zsh_win32_signal_init(void);
void zsh_win32_fix_environ(void);
void zsh_win32_init_console(void);
int  zsh_win32_console_output_fd(void);
int  zsh_win32_console_write(int fd, const void *buf, unsigned int n);
int  zsh_win32_is_wine(void);

#endif /* _WIN32_ZSH_ZSHWIN32_H */
