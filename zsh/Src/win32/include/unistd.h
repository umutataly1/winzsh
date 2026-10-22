/*
 * unistd.h - Unix-like interface for the native Win32 port of zsh
 *
 * The Microsoft C runtime's unistd.h is tiny.  This header pulls it in
 * and adds the POSIX-ish declarations zsh needs; the implementations
 * live in Src/win32/compat.c and friends.
 */

#ifndef _WIN32_ZSH_UNISTD_H
#define _WIN32_ZSH_UNISTD_H

#include_next <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <direct.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32_ZSH_UNISTD_DECLS
#define _WIN32_ZSH_UNISTD_DECLS

int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int getgroups(int size, gid_t *list);

int pipe(int fds[2]);

/*
 * Windows has no fork().  The shell emulates it by starting a fresh
 * copy of itself, so the real fork() is never available; it is declared
 * here only so that the few places that mention it still compile.  They
 * either use the emulation instead or are unreachable on Windows.
 */
pid_t fork(void);

int lstat(const char *path, struct stat *buf);
int readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *target, const char *linkpath);
int link(const char *target, const char *linkpath);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int fsync(int fd);
char *realpath(const char *path, char *resolved);
char *ttyname(int fd);
pid_t getppid(void);
int gethostname(char *name, size_t len);
char *getlogin(void);
long sysconf(int name);
int getpagesize(void);
char *basename(char *path);
char *dirname(char *path);
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
void sync(void);

/*
 * The mingw runtime declares an inline nanosleep() that calls into the
 * winpthreads library; we do not use that library, so the shell uses our
 * own implementation instead.
 */
int zsh_win32_nanosleep(const struct timespec *req, struct timespec *rem);
#define nanosleep zsh_win32_nanosleep

/*
 * The temporary file name helpers from the mingw runtime use a generator
 * that is not reset between processes, so a stale file from an earlier
 * run can make them fail for good.  Ours retries with fresh names.
 */
char *zsh_win32_mktemp(char *tmpl);
int zsh_win32_mkstemp(char *tmpl);
#define mktemp zsh_win32_mktemp
#define mkstemp zsh_win32_mkstemp

/* The directory temporary files are created in, e.g. from %TEMP%. */
char *zsh_win32_tmpdir(void);

/*
 * Reading from a console goes through our own buffered reader so that
 * select() can tell whether a character is already waiting; see
 * Src/win32/term.c.
 */
int zsh_win32_read(int fd, void *buf, unsigned int n);
#ifndef ZSH_WIN32_NO_READ_MACRO
# define read(fd, buf, n) zsh_win32_read((fd), (buf), (n))
#endif

#endif /* _WIN32_ZSH_UNISTD_DECLS */

/* sysconf() selectors. */
#ifndef _SC_ARG_MAX
# define _SC_ARG_MAX 1
# define _SC_CHILD_MAX 2
# define _SC_OPEN_MAX 3
# define _SC_NPROCESSORS_ONLN 4
# define _SC_PAGESIZE 5
# define _SC_JOB_CONTROL 6
# define _SC_IOV_MAX 7
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_UNISTD_H */
