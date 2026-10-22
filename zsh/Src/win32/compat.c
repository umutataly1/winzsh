/*
 * compat.c - the Unix compatibility layer for the native Win32 port of zsh
 *
 * Everything in this file is implemented directly on top of the Win32 API
 * and the Microsoft C runtime.  There is deliberately no dependency on any
 * zsh internals here; the parts of the port that need to know about the
 * shell live in Src/win32/zshwin32.c.
 */

#include "win32.h"

#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <unistd.h>
#include <direct.h>
#include <langinfo.h>
#include <process.h>
#include <bcrypt.h>

/*
 * <sys/stat.h> redirects stat() and fstat() to the wrappers below; this
 * file implements them, so it must see the C runtime functions.
 */
#undef stat
#undef fstat

/*
 * The layout of a reparse point as documented for the Windows API.  The
 * mingw headers do not provide REPARSE_DATA_BUFFER, so spell it out here.
 */
struct zsh_reparse_buffer {
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;
    union {
	struct {
	    WORD  SubstituteNameOffset;
	    WORD  SubstituteNameLength;
	    WORD  PrintNameOffset;
	    WORD  PrintNameLength;
	    DWORD Flags;
	    WCHAR PathBuffer[1];
	} SymbolicLinkReparseBuffer;
	struct {
	    WORD  SubstituteNameOffset;
	    WORD  SubstituteNameLength;
	    WORD  PrintNameOffset;
	    WORD  PrintNameLength;
	    WCHAR PathBuffer[1];
	} MountPointReparseBuffer;
	struct {
	    BYTE  DataBuffer[1];
	} GenericReparseBuffer;
    };
};

/*
 * Per file descriptor state that the CRT does not keep track of.
 */
struct fdinfo {
    int flags;			/* O_* bits we track ourselves */
    int fdflags;		/* FD_CLOEXEC */
    int used;
};

#define ZSH_MAXFD 1024

static struct fdinfo zsh_fdinfo[ZSH_MAXFD];

/* ------------------------------------------------------------------ */
/* Path translation						      */
/* ------------------------------------------------------------------ */

/*
 * Windows has no /dev/null, /dev/tty and friends.  Translate the ones
 * zsh actually uses into their Win32 equivalents.  The returned string is
 * allocated with malloc() and must be freed by the caller.
 */
char *
zsh_win32_path(const char *path)
{
    static const struct {
	const char *from;
	const char *to;
    } map[] = {
	{ "/dev/null",		"NUL" },
	{ "/dev/urandom",	"\\\\.\\PhysicalDrive0" },
	{ "/dev/random",	"\\\\.\\PhysicalDrive0" },
	{ NULL, NULL }
    };
    int i;

    if (!path)
	return NULL;
    for (i = 0; map[i].from; i++) {
	size_t flen = strlen(map[i].from);

	if (strncmp(path, map[i].from, flen) == 0 &&
	    (path[flen] == '\0' || path[flen] == '/')) {
	    const char *rest = path + flen;
	    size_t tlen = strlen(map[i].to);
	    size_t rlen = strlen(rest);
	    char *res = malloc(tlen + rlen + 1);

	    if (!res)
		return NULL;
	    memcpy(res, map[i].to, tlen);
	    memcpy(res + tlen, rest, rlen + 1);
	    return res;
	}
    }
    return _strdup(path);
}

/*
 * True if the path is absolute in the native Windows sense.
 *
 * Besides the Unix form, which starts with a slash, a path may start
 * with a drive letter followed by a colon and a separator, or it may be
 * a UNC name starting with two separators.  Note that just "C:" is not
 * absolute: it names the current directory of that drive.
 */
int
zsh_win32_isabs(const char *path)
{
    if (!path || !*path)
	return 0;
    if (*path == '/' || *path == '\\')
	return 1;
    return isalpha((unsigned char) path[0]) && path[1] == ':' &&
	(path[2] == '/' || path[2] == '\\');
}

/*
 * Convert the backslashes in a path to forward slashes, in place.  The
 * shell's own path manipulation (the :A and :P modifiers, the directory
 * cache and so on) only understands slashes, as in Unix paths; the
 * Windows API accepts either.
 */
void
zsh_win32_slashes(char *path)
{
    if (path)
	for (; *path; path++)
	    if (*path == '\\')
		*path = '/';
}

/*
 * True if the name looks like a Windows executable.  Used only to give
 * such files a plausible execute bit.
 */
static int
zsh_win32_isexec(const char *path)
{
    static const char *const exts[] = {
	".exe", ".com", ".bat", ".cmd", ".ps1", NULL
    };
    const char *base = path + strlen(path);
    int i;

    while (base > path && base[-1] != '/' && base[-1] != '\\')
	base--;
    for (i = 0; exts[i]; i++) {
	size_t blen = strlen(base), elen = strlen(exts[i]);

	if (blen > elen && !strcasecmp(base + blen - elen, exts[i]))
	    return 1;
    }
    return 0;
}

/*
 * Plausible POSIX mode bits for an object with the given Windows
 * attributes.  Windows permissions are ACLs, which the POSIX bits cannot
 * express; the usual mapping is used, where everybody who can read the
 * file may also write it unless it is marked read-only.
 */
static int
zsh_win32_mode(DWORD attrs, int type, const char *path)
{
    int mode;

    if (type == S_IFDIR)
	return 0755;
    mode = (attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644;
    if (path && zsh_win32_isexec(path))
	mode |= 0111;
    return mode;
}

/**/
int
zsh_win32_stat(const char *path, struct stat *buf)
{
    char *wpath;
    DWORD attrs;
    int ret;

    if (!path) {
	errno = EINVAL;
	return -1;
    }
    if (!(wpath = zsh_win32_path(path))) {
	errno = ENOMEM;
	return -1;
    }
    ret = stat(wpath, buf);
    if (ret == 0) {
	int type = buf->st_mode & S_IFMT;

	attrs = GetFileAttributesA(wpath);
	if (attrs != INVALID_FILE_ATTRIBUTES &&
	    (type == S_IFREG || type == S_IFDIR))
	    buf->st_mode = type | zsh_win32_mode(attrs, type, wpath);
    }
    free(wpath);
    return ret;
}

/**/
int
zsh_win32_fstat(int fd, struct stat *buf)
{
    int ret = fstat(fd, buf);

    if (ret == 0) {
	int type = buf->st_mode & S_IFMT;

	if (type == S_IFREG || type == S_IFDIR) {
	    HANDLE h = (HANDLE) _get_osfhandle(fd);
	    BY_HANDLE_FILE_INFORMATION info;

	    if (h != INVALID_HANDLE_VALUE &&
		GetFileInformationByHandle(h, &info))
		buf->st_mode = type |
		    zsh_win32_mode(info.dwFileAttributes, type, NULL);
	}
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* open() / fcntl()						      */
/* ------------------------------------------------------------------ */

/*
 * True if the name refers to a character device rather than a file or a
 * pipe.  The C runtime refuses to create or truncate such a name.
 */
static int
zsh_win32_is_device(const char *path)
{
    static const char *const devs[] = {
	"NUL", "CON", "CONIN$", "CONOUT$", "AUX", "PRN",
	"COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", NULL
    };
    int i;

    if (!path)
	return 0;
    if (!strncmp(path, "\\\\.\\", 4))
	return 1;
    for (i = 0; devs[i]; i++)
	if (!strcasecmp(path, devs[i]))
	    return 1;
    return 0;
}

/**/
int
zsh_win32_open(const char *path, int flags, ...)
{
    int mode = 0;
    int acc = flags & O_ACCMODE;
    char *wpath;
    int fd;

    if (flags & O_CREAT) {
	va_list ap;
	va_start(ap, flags);
	mode = va_arg(ap, int);
	va_end(ap);
    }

    /*
     * The pseudo file /dev/fd/N refers to the file descriptor N; it
     * is used by the shell to open the other end of a pipe it has just
     * created for a process substitution.
     */
    if (path && path[0] == '/' && !strncmp(path, "/dev/fd/", 8)) {
	int n = atoi(path + 8);

	if (n < 0)
	    n = 0;
	fd = _dup(n);
	if (fd < 0) {
	    errno = EBADF;
	    return -1;
	}
	if (fd < ZSH_MAXFD) {
	    zsh_fdinfo[fd].used = 1;
	    zsh_fdinfo[fd].flags = acc | (acc == O_RDONLY ? O_TEXT
							     : O_BINARY);
	    zsh_fdinfo[fd].fdflags = 0;
	}
	return fd;
    }

    /*
     * /dev/stdin, /dev/stdout and /dev/stderr are the standard stream
     * of the shell; they are duplicated so that closing the opened
     * descriptor does not disturb the stream itself.
     */
    if (path) {
	static const struct {
	    const char *name;
	    int fd;
	} stds[] = {
	    { "/dev/stdin",  0 },
	    { "/dev/stdout", 1 },
	    { "/dev/stderr", 2 },
	    { NULL, 0 }
	};
	int i;

	for (i = 0; stds[i].name; i++) {
	    if (strcmp(path, stds[i].name))
		continue;
	    fd = _dup(stds[i].fd);
	    if (fd < 0) {
		errno = EBADF;
		return -1;
	    }
	    if (fd < ZSH_MAXFD) {
		zsh_fdinfo[fd].used = 1;
		zsh_fdinfo[fd].flags = acc | (acc == O_RDONLY ? O_TEXT
							     : O_BINARY);
		zsh_fdinfo[fd].fdflags = 0;
	    }
	    return fd;
	}
    }

    /*
     * Read-only opens use the CRT's text mode so that CRLF line endings
     * are translated to LF; this makes DOS/Windows scripts and redirected
     * input parse correctly.  Everything else is binary so that output
     * uses Unix line endings.
     */
    flags &= ~(O_TEXT | O_BINARY);
    flags |= (acc == O_RDONLY) ? O_TEXT : O_BINARY;

    /*
     * /dev/tty is the console: reading comes from the input side,
     * writing goes to the output side.
     */
    if (path && !strcmp(path, "/dev/tty"))
	wpath = _strdup(acc == O_RDONLY ? "CONIN$" : "CONOUT$");
    else
	wpath = zsh_win32_path(path);
    if (!wpath) {
	errno = ENOENT;
	return -1;
    }
    /*
     * Creating or truncating a device does not make sense, and the C
     * runtime refuses it with EINVAL; devices are opened as they are.
     */
    if (zsh_win32_is_device(wpath))
	flags &= ~(O_CREAT | O_EXCL | O_TRUNC | O_APPEND);
    fd = _open(wpath, flags, mode);
    if (fd >= 0 && fd < ZSH_MAXFD) {
	zsh_fdinfo[fd].used = 1;
	zsh_fdinfo[fd].flags = acc | (flags & (O_APPEND | O_BINARY | O_TEXT));
	zsh_fdinfo[fd].fdflags = 0;
    }
    free(wpath);
    return fd;
}

/**/
static int
zsh_fd_base_flags(int fd)
{
    if (fd >= 0 && fd < ZSH_MAXFD && zsh_fdinfo[fd].used)
	return zsh_fdinfo[fd].flags;
    return 0;
}

/*
 * Duplicate the descriptor "fd" onto the lowest free descriptor that is
 * at least "arg".  The Microsoft C runtime's _dup() always returns the
 * lowest free descriptor, so the descriptors below "arg" are filled up
 * with placeholders first.
 */
static int
zsh_win32_dupfd(int fd, int arg)
{
    int dummies[128];
    int ndummy = 0;
    int t = -1;

    if (arg < 0)
	arg = 0;
    for (;;) {
	t = _open("NUL", _O_RDONLY | _O_BINARY);
	if (t < 0) {
	    /* Out of descriptors: fall back to a plain duplicate. */
	    t = _dup(fd);
	    if (t < 0 || t < arg) {
		if (t >= 0)
		    _close(t);
		t = -1;
	    }
	    break;
	}
	if (t >= arg) {
	    /* t is a free descriptor at or above arg: use it. */
	    if (_dup2(fd, t) < 0) {
		_close(t);
		t = -1;
	    }
	    break;
	}
	if (ndummy >= (int) (sizeof(dummies) / sizeof(dummies[0]))) {
	    _close(t);
	    t = -1;
	    break;
	}
	dummies[ndummy++] = t;
    }
    while (ndummy > 0)
	_close(dummies[--ndummy]);
    return t;
}

/**/
int
zsh_win32_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg = 0;
    long l;

    va_start(ap, cmd);
    if (cmd != F_GETFL && cmd != F_GETFD)
	arg = va_arg(ap, int);
    va_end(ap);

    switch (cmd) {
    case F_GETFL:
	return zsh_fd_base_flags(fd);
    case F_SETFL:
	if (fd >= 0 && fd < ZSH_MAXFD)
	    zsh_fdinfo[fd].flags = (zsh_fdinfo[fd].flags & ~O_ACCMODE) |
		(arg & O_ACCMODE);
	return 0;
    case F_GETFD:
	if (fd >= 0 && fd < ZSH_MAXFD)
	    return zsh_fdinfo[fd].fdflags;
	return 0;
    case F_SETFD:
	if (fd >= 0 && fd < ZSH_MAXFD)
	    zsh_fdinfo[fd].fdflags = arg;
	return 0;
    case F_DUPFD:
	/* Duplicate onto the lowest free descriptor at or above arg. */
	{
	    int newfd = zsh_win32_dupfd(fd, arg);
	    if (newfd >= 0 && newfd < ZSH_MAXFD) {
		zsh_fdinfo[newfd].used = 1;
		zsh_fdinfo[newfd].flags = zsh_fd_base_flags(fd);
		zsh_fdinfo[newfd].fdflags = 0;
	    }
	    return newfd;
	}
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK:
	/*
	 * Record locking is not supported natively; report success so
	 * that callers (the history file locking in particular) do not
	 * complain.  The lock is not actually taken.
	 */
	return 0;
    default:
	errno = EINVAL;
	return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Environment							      */
/* ------------------------------------------------------------------ */

/**/
int
setenv(const char *name, const char *value, int overwrite)
{
    size_t len;
    char *buf;

    if (!name || !*name) {
	errno = EINVAL;
	return -1;
    }
    if (!overwrite && getenv(name))
	return 0;
    len = strlen(name) + strlen(value ? value : "") + 2;
    buf = malloc(len);
    if (!buf) {
	errno = ENOMEM;
	return -1;
    }
    sprintf(buf, "%s=%s", name, value ? value : "");
    if (_putenv(buf) < 0) {
	free(buf);
	errno = ENOMEM;
	return -1;
    }
    /* _putenv() keeps the pointer, so do not free buf here. */
    return 0;
}

/**/
int
unsetenv(const char *name)
{
    if (!name || !*name) {
	errno = EINVAL;
	return -1;
    }
    if (_putenv((char *)name) < 0) {	/* "NAME=" removes it */
	errno = EINVAL;
	return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pipes								      */
/* ------------------------------------------------------------------ */

/**/
int
pipe(int fds[2])
{
    if (_pipe(fds, 65536, _O_BINARY) < 0) {
	errno = EINVAL;
	return -1;
    }
    return 0;
}

/**/
pid_t
fork(void)
{
    /*
     * Windows has no fork().  The shell emulates a subshell by
     * starting a fresh copy of itself, so this is only here to keep
     * the few places that mention fork() compiling.  They either use
     * the emulation instead or are unreachable on Windows.
     */
    errno = ENOSYS;
    return -1;
}

/* ------------------------------------------------------------------ */
/* File system odds and ends					      */
/* ------------------------------------------------------------------ */

/**/
int
lstat(const char *path, struct stat *buf)
{
    /* Windows has no POSIX symlinks; stat() is the best we can do. */
    return zsh_win32_stat(path, buf);
}

/**/
int
readlink(const char *path, char *buf, size_t bufsiz)
{
    /*
     * Resolve symbolic links the hard way: open the file with
     * FILE_FLAG_OPEN_REPARSE_POINT and ask for the reparse point.
     */
    HANDLE h;
    BYTE *bufp = NULL;
    DWORD ret = 0, n;
    struct zsh_reparse_buffer *rd;
    wchar_t *wpath;
    int len, rc = -1;
    size_t wlen = strlen(path) + 1;

    wpath = malloc(wlen * sizeof(wchar_t));
    if (!wpath)
	return -1;
    if (MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, wlen) <= 0) {
	free(wpath);
	errno = EINVAL;
	return -1;
    }
    h = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    NULL, OPEN_EXISTING,
		    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) {
	errno = ENOENT;
	return -1;
    }
    n = sizeof(struct zsh_reparse_buffer) + MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
    bufp = malloc(n);
    if (!bufp) {
	CloseHandle(h);
	errno = ENOMEM;
	return -1;
    }
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, bufp, n, &ret,
			 NULL)) {
	free(bufp);
	CloseHandle(h);
	errno = EINVAL;
	return -1;
    }
    rd = (struct zsh_reparse_buffer *) bufp;
    if (rd->ReparseTag == IO_REPARSE_TAG_SYMLINK ||
	rd->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
	/* SubstituteName is "\??\C:\path"; strip the prefix. */
	wchar_t *sub = (wchar_t *) ((char *) rd +
	    rd->SymbolicLinkReparseBuffer.SubstituteNameOffset);
	size_t slen = rd->SymbolicLinkReparseBuffer.SubstituteNameLength /
	    sizeof(wchar_t);
	char *out;
	int i;
	size_t skip = 0;

	while (skip < slen && sub[skip] == L'\\')
	    skip++;
	if (slen - skip > 4 && sub[skip] == L'?' && sub[skip+1] == L'?')
	    skip += 4;
	len = (int)(slen - skip);
	out = malloc(len + 1);
	if (out) {
	    for (i = 0; i < len; i++) {
		if (sub[skip + i] > 255)
		    out[i] = '?';
		else
		    out[i] = (char) sub[skip + i];
	    }
	    out[len] = '\0';
	    if ((size_t) len >= bufsiz)
		len = (int) bufsiz - 1;
	    memcpy(buf, out, len);
	    buf[len] = '\0';
	    rc = len;
	    free(out);
	}
    } else {
	errno = EINVAL;
    }
    free(bufp);
    CloseHandle(h);
    return rc;
}

/**/
int
symlink(const char *target, const char *linkpath)
{
    wchar_t *wt, *wl;
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    int rc;

    wt = malloc((strlen(target) + 1) * sizeof(wchar_t));
    wl = malloc((strlen(linkpath) + 1) * sizeof(wchar_t));
    if (!wt || !wl) {
	free(wt);
	free(wl);
	errno = ENOMEM;
	return -1;
    }
    MultiByteToWideChar(CP_ACP, 0, target, -1, wt, strlen(target) + 1);
    MultiByteToWideChar(CP_ACP, 0, linkpath, -1, wl, strlen(linkpath) + 1);
    rc = CreateSymbolicLinkW(wl, wt, flags);
    free(wt);
    free(wl);
    if (!rc) {
	errno = EEXIST;
	return -1;
    }
    return 0;
}

/**/
int
link(const char *target, const char *linkpath)
{
    wchar_t *wt, *wl;
    int rc;

    wt = malloc((strlen(target) + 1) * sizeof(wchar_t));
    wl = malloc((strlen(linkpath) + 1) * sizeof(wchar_t));
    if (!wt || !wl) {
	free(wt);
	free(wl);
	errno = ENOMEM;
	return -1;
    }
    MultiByteToWideChar(CP_ACP, 0, target, -1, wt, strlen(target) + 1);
    MultiByteToWideChar(CP_ACP, 0, linkpath, -1, wl, strlen(linkpath) + 1);
    rc = CreateHardLinkW(wl, wt, NULL);
    free(wt);
    free(wl);
    if (!rc) {
	errno = EEXIST;
	return -1;
    }
    return 0;
}

/**/
int
chown(const char *path, uid_t owner, gid_t group)
{
    UNUSED_ARG(path); UNUSED_ARG(owner); UNUSED_ARG(group);
    return 0;
}

/**/
int
lchown(const char *path, uid_t owner, gid_t group)
{
    UNUSED_ARG(path); UNUSED_ARG(owner); UNUSED_ARG(group);
    return 0;
}

/**/
int
fchown(int fd, uid_t owner, gid_t group)
{
    UNUSED_ARG(fd); UNUSED_ARG(owner); UNUSED_ARG(group);
    return 0;
}

/**/
char *
realpath(const char *path, char *resolved)
{
    char *buf;
    DWORD n;

    buf = resolved ? resolved : malloc(MAX_PATH * 2);
    if (!buf) {
	errno = ENOMEM;
	return NULL;
    }
    n = GetFullPathNameA(path, MAX_PATH * 2, buf, NULL);
    if (n == 0 || n >= MAX_PATH * 2) {
	if (!resolved)
	    free(buf);
	errno = ENOENT;
	return NULL;
    }
    zsh_win32_slashes(buf);
    return buf;
}

/*
 * The getcwd() the shell sees.  The Microsoft C runtime returns the
 * current directory with backslashes, while the rest of the shell
 * expects slashes, so the result is converted here.
 */
/**/
char *
zsh_win32_getcwd(char *buf, size_t size)
{
    char *res = _getcwd(buf, (int) size);

    if (res)
	zsh_win32_slashes(res);
    return res;
}

/**/
int
fsync(int fd)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);

    if (h == INVALID_HANDLE_VALUE) {
	errno = EBADF;
	return -1;
    }
    if (!FlushFileBuffers(h)) {
	errno = EIO;
	return -1;
    }
    return 0;
}

/*
 * The POSIX mkdir(); the one declared by the C runtime takes only the
 * name of the directory.  The mode is ignored, as Windows has no file
 * permissions in the POSIX sense.
 */
/**/
int
zsh_win32_mkdir(const char *path, mode_t mode)
{
    static const struct {
	DWORD win;
	int posix;
    } errs[] = {
	{ ERROR_ALREADY_EXISTS, EEXIST },
	{ ERROR_FILE_EXISTS, EEXIST },
	{ ERROR_ACCESS_DENIED, EACCES },
	{ ERROR_PATH_NOT_FOUND, ENOENT },
	{ ERROR_FILE_NOT_FOUND, ENOENT },
	{ ERROR_DISK_FULL, ENOSPC },
	{ ERROR_INVALID_NAME, EINVAL },
    };
    int i;

    UNUSED_ARG(mode);
    if (CreateDirectoryA(path, NULL))
	return 0;
    errno = EIO;
    for (i = 0; i < (int) (sizeof(errs) / sizeof(errs[0])); i++)
	if (GetLastError() == errs[i].win) {
	    errno = errs[i].posix;
	    break;
	}
    return -1;
}

/*
 * POSIX sync(): flush the buffers of the C library and ask the system to
 * flush the file system of every mounted drive.
 */
/**/
void
sync(void)
{
    int drive;

    _flushall();
    for (drive = 0; drive < 26; drive++) {
	char root[4];
	HANDLE h;

	root[0] = (char) ('A' + drive);
	root[1] = ':';
	root[2] = '\\';
	root[3] = '\0';
	h = CreateFileA(root, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h != INVALID_HANDLE_VALUE) {
	    FlushFileBuffers(h);
	    CloseHandle(h);
	}
    }
}

/**/
pid_t
getppid(void)
{
    /*
     * Windows does not have a parent process id that is useful for a
     * shell; the value of the environment variable is used instead when
     * it is set, otherwise the shell pretends to have been started by
     * the process that started it.
     */
    const char *ppid = getenv("ZSH_WIN32_PPID");

    if (ppid && *ppid)
	return (pid_t) atoi(ppid);
    return (pid_t) 1;
}

/**/
char *
ttyname(int fd)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD type;

    if (h == INVALID_HANDLE_VALUE)
	return NULL;
    if (!GetConsoleMode(h, &type))
	return NULL;
    return "CONIN$";
}

/**/
int
gethostname(char *name, size_t len)
{
    DWORD n = (DWORD) len;
    const char *env = getenv("COMPUTERNAME");

    if (env && *env) {
	if (strlen(env) >= len)
	    return -1;
	strcpy(name, env);
	return 0;
    }
    if (!GetComputerNameA(name, &n)) {
	errno = ENOSYS;
	return -1;
    }
    return 0;
}

/**/
char *
getlogin(void)
{
    const char *env = getenv("USERNAME");

    if (!env || !*env)
	env = getenv("USER");
    if (!env || !*env)
	return NULL;
    return (char *) env;
}

/*
 * Windows has no user or group ids; everything runs as the user that
 * started the shell, so the ids are all zero.
 */
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int setuid(uid_t uid) { UNUSED_ARG(uid); return 0; }
int setgid(gid_t gid) { UNUSED_ARG(gid); return 0; }
int seteuid(uid_t uid) { UNUSED_ARG(uid); return 0; }
int setegid(gid_t gid) { UNUSED_ARG(gid); return 0; }
int setreuid(uid_t r, uid_t e) { UNUSED_ARG(r); UNUSED_ARG(e); return 0; }
int setregid(gid_t r, gid_t e) { UNUSED_ARG(r); UNUSED_ARG(e); return 0; }
int setresuid(uid_t r, uid_t e, uid_t s)
{
    UNUSED_ARG(r); UNUSED_ARG(e); UNUSED_ARG(s);
    return 0;
}
int setresgid(gid_t r, gid_t e, gid_t s)
{
    UNUSED_ARG(r); UNUSED_ARG(e); UNUSED_ARG(s);
    return 0;
}

/*
 * Windows has no supplementary groups as far as the shell is concerned:
 * the process always runs with the group of the user.  The list is
 * therefore empty, which is what getgroups(0, NULL) reports.
 */
int getgroups(int size, gid_t *list)
{
    UNUSED_ARG(size); UNUSED_ARG(list);
    return 0;
}

/**/
int
uname(struct utsname *name)
{
    memset(name, 0, sizeof(*name));
    strcpy(name->sysname, "Windows");
    strcpy(name->nodename, "localhost");
    strcpy(name->release, "10.0");
    strcpy(name->version, "10.0");
    strcpy(name->machine, "x86_64");
    strcpy(name->domainname, "");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Resource usage and limits					      */
/* ------------------------------------------------------------------ */

/**/
clock_t
times(struct tms *buf)
{
    FILETIME creation, exit, kernel, user;
    ULARGE_INTEGER k, u;
    clock_t ticks = clock();

    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
	k.LowPart = kernel.dwLowDateTime;
	k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime;
	u.HighPart = user.dwHighDateTime;
	if (buf) {
	    buf->tms_utime = (clock_t) ((u.QuadPart - k.QuadPart) / 10000);
	    buf->tms_stime = (clock_t) (k.QuadPart / 10000);
	    buf->tms_cutime = 0;
	    buf->tms_cstime = 0;
	}
    } else if (buf) {
	memset(buf, 0, sizeof(*buf));
    }
    return ticks;
}

/**/
int
getrusage(int who, struct rusage *usage)
{
    FILETIME creation, exit, kernel, user;
    ULARGE_INTEGER k, u;
    HANDLE h;

    UNUSED_ARG(who);
    if (!usage)
	return 0;
    memset(usage, 0, sizeof(*usage));
    h = (who == RUSAGE_SELF) ? GetCurrentProcess() : GetCurrentProcess();
    if (!GetProcessTimes(h, &creation, &exit, &kernel, &user))
	return -1;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    usage->ru_utime.tv_sec = (long) (u.QuadPart / 10000000ULL);
    usage->ru_utime.tv_usec = (long) ((u.QuadPart % 10000000ULL) / 10);
    usage->ru_stime.tv_sec = (long) (k.QuadPart / 10000000ULL);
    usage->ru_stime.tv_usec = (long) ((k.QuadPart % 10000000ULL) / 10);
    return 0;
}

/**/
int
getrlimit(int resource, struct rlimit *rlim)
{
    if (!rlim)
	return -1;
    if (resource == RLIMIT_NOFILE) {
	/*
	 * The number of descriptors the shell's compatibility layer can
	 * keep track of; Windows itself puts no limit on open handles.
	 */
	rlim->rlim_cur = ZSH_MAXFD;
	rlim->rlim_max = ZSH_MAXFD;
	return 0;
    }
    rlim->rlim_cur = RLIM_INFINITY;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

/**/
int
setrlimit(int resource, const struct rlimit *rlim)
{
    UNUSED_ARG(resource); UNUSED_ARG(rlim);
    return 0;
}

/**/
long
sysconf(int name)
{
    SYSTEM_INFO si;

    switch (name) {
    case _SC_ARG_MAX:
	return 32767;
    case _SC_CHILD_MAX:
	return 0;
    case _SC_OPEN_MAX:
	return 1024;
    case _SC_PAGESIZE:
	GetSystemInfo(&si);
	return si.dwPageSize;
    case _SC_NPROCESSORS_ONLN:
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
    case _SC_JOB_CONTROL:
	return 1;
    case _SC_IOV_MAX:
	return 16;
    default:
	errno = EINVAL;
	return -1;
    }
}

/**/
int
getpagesize(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwPageSize;
}

/**/
int
zsh_win32_nanosleep(const struct timespec *req, struct timespec *rem)
{
    DWORD ms;

    if (!req) {
	errno = EINVAL;
	return -1;
    }
    ms = (DWORD) (req->tv_sec * 1000 + req->tv_nsec / 1000000);
    if (ms == 0)
	ms = 1;
    Sleep(ms);
    if (rem) {
	rem->tv_sec = 0;
	rem->tv_nsec = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* basename() / dirname()					      */
/* ------------------------------------------------------------------ */

/**/
char *
basename(char *path)
{
    char *p = strrchr(path, '/');
    char *q = strrchr(path, '\\');

    if (!p)
	p = q;
    else if (q && q > p)
	p = q;
    return p ? p + 1 : path;
}

/**/
char *
dirname(char *path)
{
    char *p = strrchr(path, '/');
    char *q = strrchr(path, '\\');
    char *end;

    if (!p)
	p = q;
    else if (q && q > p)
	p = q;
    if (!p)
	return ".";
    end = p;
    while (end > path && (end[-1] == '/' || end[-1] == '\\'))
	end--;
    if (end == path) {
	if (path[0] == '/' || path[0] == '\\')
	    path[1] = '\0';
	else
	    path[0] = '\0';
	return path;
    }
    *end = '\0';
    return path;
}

/* ------------------------------------------------------------------ */
/* Command name resolution					      */
/* ------------------------------------------------------------------ */

/*
 * Complete a command name with an extension from PATHEXT, the way the
 * Windows command processor finds "cmd.exe" when "cmd" is typed.
 *
 * If "name" has no extension, write the first name+extension that exists
 * and is not a directory to "buf" (of size "len") and return 1.  If
 * there is no match, or the name already has an extension, return 0 and
 * leave "buf" alone.  "buf" must not alias "name".
 */
/**/
int
zsh_win32_appext(const char *name, char *buf, size_t len)
{
    const char *base = name, *p, *end, *exts;
    size_t nlen, el;

    for (p = name; *p; p++)
	if (*p == '/' || *p == '\\')
	    base = p + 1;
    if (strchr(base, '.'))
	return 0;
    nlen = (size_t)(p - name);
    if (!nlen || nlen + 1 >= len)
	return 0;

    exts = getenv("PATHEXT");
    if (!exts || !*exts)
	exts = ".COM;.EXE;.BAT;.CMD";

    for (p = exts; *p; p = end ? end + 1 : NULL) {
	DWORD attr;

	end = strchr(p, ';');
	el = end ? (size_t)(end - p) : strlen(p);
	if (el && nlen + el < len) {
	    memcpy(buf, name, nlen);
	    memcpy(buf + nlen, p, el);
	    buf[nlen + el] = '\0';
	    attr = GetFileAttributesA(buf);
	    if (attr != INVALID_FILE_ATTRIBUTES &&
		!(attr & FILE_ATTRIBUTE_DIRECTORY))
		return 1;
	}
	if (!end)
	    break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The system random number generator				      */
/* ------------------------------------------------------------------ */

/**/
ssize_t
getrandom(void *buf, size_t buflen, unsigned int flags)
{
    long st;

    if (buflen > 0xffffffffUL) {
	errno = EINVAL;
	return -1;
    }
    st = (long) BCryptGenRandom(NULL, (PUCHAR) buf, (ULONG) buflen,
				BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st < 0) {
	errno = EIO;
	return -1;
    }
    return (ssize_t) buflen;
}

/* ------------------------------------------------------------------ */
/* Temporary files						      */
/* ------------------------------------------------------------------ */

/*
 * Windows keeps the per-user temporary directory in TEMP (or TMP).  zsh
 * appends "XXXXXX" to TMPPREFIX, so use a subdirectory that is created
 * on demand; this keeps the shell's temporary files out of the current
 * directory.
 */
/**/
char *
zsh_win32_tmpdir(void)
{
    static char buf[1024];
    static int done;
    const char *p;
    char *s;

    if (done)
	return buf;
    done = 1;
    p = getenv("TEMP");
    if (!p || !*p)
	p = getenv("TMP");
    if (!p || !*p)
	p = getenv("USERPROFILE");
    if (!p || !*p)
	p = ".";
    snprintf(buf, sizeof(buf), "%s/zsh", p);
    for (s = buf; *s; s++)
	if (*s == '\\')
	    *s = '/';
    _mkdir(buf);
    return buf;
}

/*
 * A small random number generator for temporary names.  It is seeded
 * from the system generator once per process.
 */
static unsigned int zsh_tmp_seed;
static int zsh_tmp_seeded;

static unsigned int
zsh_tmp_rand(void)
{
    if (!zsh_tmp_seeded) {
	unsigned int s = 0;

	if (getrandom(&s, sizeof(s), 0) != (ssize_t) sizeof(s))
	    s = (unsigned int) GetTickCount() ^
		((unsigned int) _getpid() << 8);
	zsh_tmp_seed = s ? s : 12345u;
	zsh_tmp_seeded = 1;
    }
    zsh_tmp_seed = zsh_tmp_seed * 1103515245u + 12345u;
    return zsh_tmp_seed >> 8;
}

static const char zsh_tmp_chars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static int
zsh_tmp_fill(char *tmpl)
{
    size_t len = strlen(tmpl), i;

    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX"))
	return 0;
    for (i = 0; i < 6; i++)
	tmpl[len - 6 + i] =
	    zsh_tmp_chars[zsh_tmp_rand() % (sizeof(zsh_tmp_chars) - 1)];
    return 1;
}

/*
 * The runtime's own mktemp()/mkstemp() reuse the same name sequence in
 * every process, so a leftover file makes them fail.  Retry instead.
 */
/**/
char *
zsh_win32_mktemp(char *tmpl)
{
    int tries;

    for (tries = 0; tries < 100; tries++) {
	if (!zsh_tmp_fill(tmpl))
	    return NULL;
	if (_access(tmpl, 0) != 0)
	    return tmpl;
    }
    return NULL;
}

/**/
int
zsh_win32_mkstemp(char *tmpl)
{
    int tries;

    for (tries = 0; tries < 100; tries++) {
	int fd;

	if (!zsh_tmp_fill(tmpl)) {
	    errno = EINVAL;
	    return -1;
	}
	fd = _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, 0600);
	if (fd >= 0)
	    return fd;
	if (errno != EEXIST)
	    return -1;
    }
    errno = EEXIST;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Locale information						      */
/* ------------------------------------------------------------------ */

/*
 * The shell speaks UTF-8.  The OS locale is only consulted to decide how
 * \u and \U escapes are encoded, and UTF-8 is always the right answer.
 */
/**/
char *
zsh_win32_nl_langinfo(int item)
{
    switch (item) {
    case CODESET:
	return "UTF-8";
    default:
	return "";
    }
}
