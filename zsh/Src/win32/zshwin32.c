/*
 * zshwin32.c - native Windows support that needs the shell's own data
 *
 * This file holds the parts of the Windows port that cannot live in the
 * compatibility layer because they need access to zsh's internal data
 * structures.
 *
 * The most important of them is the emulation of fork().  Windows has
 * no fork(), so a subshell is created by starting a fresh copy of the
 * shell: the parent serialises the state of the shell (options,
 * aliases, functions, traps and parameters) into a file, the child
 * reads that file back and then runs the command it was given.
 *
 * The state file has the following format:
 *
 *	#zsh-win32-state 1
 *	@options <hexadecimal dump of the option array>
 *	@script
 *	<shell code restoring aliases, functions, traps and parameters>
 *
 * The child restores the options directly (so that no shell code can
 * fail to apply them) and executes the script with the shell itself.
 */

#ifndef NOMINMAX
# define NOMINMAX 1
#endif

#include "win32.h"

#include "zsh.mdh"

#include "zshwin32.h"

/* ------------------------------------------------------------------ */
/* Small helpers						      */
/* ------------------------------------------------------------------ */

struct zbuf {
    char *buf;
    size_t len;
    size_t size;
};

static void
zbuf_init(struct zbuf *b)
{
    b->size = 4096;
    b->len = 0;
    b->buf = malloc(b->size);
    if (b->buf)
	b->buf[0] = '\0';
    else
	b->size = 0;
}

static void
zbuf_free(struct zbuf *b)
{
    free(b->buf);
    b->buf = NULL;
    b->len = b->size = 0;
}

static void
zbuf_add(struct zbuf *b, const char *s, size_t n)
{
    if (!b->buf || !n)
	return;
    if (b->len + n + 1 > b->size) {
	size_t ns = b->size ? b->size : 4096;
	while (b->len + n + 1 > ns)
	    ns *= 2;
	b->buf = realloc(b->buf, ns);
	if (!b->buf) {
	    b->size = b->len = 0;
	    return;
	}
	b->size = ns;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void
zbuf_str(struct zbuf *b, const char *s)
{
    if (s)
	zbuf_add(b, s, strlen(s));
}

/*
 * Run "fn" with stdout redirected into a temporary file and return what
 * it printed in a malloc()ed string (which the caller has to free).
 * Returns NULL on failure.
 *
 * Note that the file is read back through the descriptor of the
 * temporary file itself: a duplicated descriptor has a file position of
 * its own on Windows, so the position of the descriptor that was
 * written to cannot be used to find the end of the data.
 */
static char *
zsh_win32_capture(void (*fn)(void *), void *arg)
{
    FILE *tmp;
    int saved, fd;
    long size;
    struct zbuf b;

    tmp = tmpfile();
    if (!tmp)
	return NULL;
    zbuf_init(&b);
    fflush(stdout);
    saved = dup(fileno(stdout));
    if (saved == -1) {
	fclose(tmp);
	zbuf_free(&b);
	return NULL;
    }
    fd = fileno(tmp);
    dup2(fd, fileno(stdout));
    fn(arg);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    size = _lseeki64(fd, 0, SEEK_END);
    if (size > 0) {
	_lseeki64(fd, 0, SEEK_SET);
	while (size > 0) {
	    char chunk[4096];
	    unsigned int want = (unsigned int)
		((size_t) size < sizeof(chunk) ? (size_t) size : sizeof(chunk));
	    int n = read(fd, chunk, want);
	    if (n <= 0)
		break;
	    zbuf_add(&b, chunk, (size_t) n);
	    size -= n;
	}
    }
    fclose(tmp);
    return b.buf;
}

/* ------------------------------------------------------------------ */
/* Dumping the state of the shell				      */
/* ------------------------------------------------------------------ */

static void
zsh_win32_dump_options(void *unused)
{
    int i;

    for (i = 0; i < OPT_SIZE; i++)
	printf("%02x", (unsigned char) opts[i]);
    putchar('\n');
}

static void
zsh_win32_dump_aliases(void *unused)
{
    scanhashtable(aliastab, 1, 0, 0, aliastab->printnode, PRINT_LIST);
}

static void
zsh_win32_dump_functions(void *unused)
{
    scanhashtable(shfunctab, 1, 0, DISABLED, shfunctab->printnode, 0);
}

static void
zsh_win32_dump_modules(void *unused)
{
    /*
     * The same flags as "zmodload -FL": emit re-executable zmodload
     * commands, using -F with an explicit feature list for modules that
     * were loaded only in part.
     */
    enum {
	WIN32_PRINTMOD_LIST = 0x0001,
	WIN32_PRINTMOD_FEATURES = 0x0010
    };

    scanhashtable(modulestab, 1, 0, MOD_UNLOAD|MOD_ALIAS,
		  modulestab->printnode,
		  WIN32_PRINTMOD_LIST|WIN32_PRINTMOD_FEATURES);
}

static void
zsh_win32_dump_traps(void *unused)
{
    int sig;

    for (sig = 0; sig < TRAPCOUNT; sig++) {
	int flags = sigtrapped[sig] & ZSIG_MASK;
	const char *name;

	if (!flags)
	    continue;
	name = getsigname(sig);
	if (flags & ZSIG_IGNORED) {
	    printf("trap -- '' %s\n", name);
	    continue;
	}
	if (siglists[sig]) {
	    char *s = getpermtext(siglists[sig], NULL, 0);
	    printf("trap -- ");
	    quotedzputs(s, stdout);
	    printf(" %s\n", name);
	    zsfree(s);
	    continue;
	}
	/*
	 * A function trap: the TRAPxxx function itself is dumped with
	 * the other functions, so there is nothing else to do here.
	 */
    }
}

/*
 * Turn the "typeset ..." line that describes a parameter into a
 * "typeset -g ..." line that can be read back in a subshell.  Returns a
 * malloc()ed string, or NULL if the parameter must not be restored.
 */
static char *
zsh_win32_param_text(const char *text)
{
    size_t len;
    char *res;

    if (!text)
	return NULL;
    if (!strncmp(text, "export ", 7) || !strncmp(text, "local ", 6))
	/* Exported parameters are inherited through the environment. */
	return NULL;
    if (!strncmp(text, "typeset -g ", 11))
	return strdup(text);
    if (strncmp(text, "typeset ", 8))
	return NULL;

    /* "typeset ..." -> "typeset -g ..." */
    len = strlen(text);
    res = malloc(len + 4);
    if (!res)
	return NULL;
    memcpy(res, "typeset -g ", 11);
    memcpy(res + 11, text + 8, len - 8 + 1);
    return res;
}

/*
 * Print the description of every parameter that has to survive into a
 * subshell.  Each description is preceded by a marker character so that
 * the individual parameters can be told apart again afterwards; the
 * marker cannot occur in the output of printparamnode().
 */
#define ZSH_WIN32_PARAM_MARKER	'\001'

static void
zsh_win32_dump_params(void *unused)
{
    int i;

    for (i = 0; i < paramtab->hsize; i++) {
	HashNode hn;

	for (hn = paramtab->nodes[i]; hn; hn = hn->next) {
	    Param p = (Param) hn;

	    if (p->node.flags & (PM_SPECIAL|PM_TIED|PM_READONLY|
				 PM_AUTOLOAD|PM_NAMEREF|PM_UNSET))
		continue;
	    if (p->node.flags & PM_EXPORTED)
		/* Inherited through the environment. */
		continue;
	    if (!p->node.nam || !*p->node.nam)
		continue;

	    putchar(ZSH_WIN32_PARAM_MARKER);
	    paramtab->printnode(&p->node, PRINT_TYPESET|PRINT_WITH_NAMESPACE);
	    putchar('\n');
	}
    }
}

/*
 * Split the output of zsh_win32_dump_params() into the individual
 * parameters and add the ones that can be restored to "out".
 */
static void
zsh_win32_add_params(const char *raw, struct zbuf *out)
{
    const char *p = raw;

    while (p && *p) {
	const char *end = strchr(p, ZSH_WIN32_PARAM_MARKER);
	const char *stop;
	char *text;

	if (!end)
	    break;
	p = end + 1;
	end = strchr(p, ZSH_WIN32_PARAM_MARKER);
	stop = end ? end : p + strlen(p);
	/* Drop the trailing newline of the description. */
	while (stop > p && stop[-1] == '\n')
	    stop--;
	{
	    char *one = malloc((size_t) (stop - p) + 1);
	    if (!one)
		continue;
	    memcpy(one, p, (size_t) (stop - p));
	    one[stop - p] = '\0';
	    text = zsh_win32_param_text(one);
	    free(one);
	}
	if (text) {
	    zbuf_str(out, text);
	    zbuf_str(out, "\n");
	    free(text);
	}
	p = end;
    }
}

/*
 * Build the text of the state file.  The caller has to free it.
 */
static char *
zsh_win32_build_state(void)
{
    struct zbuf b, params;
    char *opts_text, *aliases, *functions, *traps, *modules, *raw_params;

    opts_text = zsh_win32_capture(zsh_win32_dump_options, NULL);
    aliases = zsh_win32_capture(zsh_win32_dump_aliases, NULL);
    functions = zsh_win32_capture(zsh_win32_dump_functions, NULL);
    modules = zsh_win32_capture(zsh_win32_dump_modules, NULL);
    traps = zsh_win32_capture(zsh_win32_dump_traps, NULL);
    raw_params = zsh_win32_capture(zsh_win32_dump_params, NULL);
    if (!opts_text || !aliases || !functions || !modules || !traps ||
	!raw_params) {
	free(opts_text);
	free(aliases);
	free(functions);
	free(modules);
	free(traps);
	free(raw_params);
	return NULL;
    }

    zbuf_init(&params);
    zsh_win32_add_params(raw_params, &params);

    zbuf_init(&b);
    zbuf_str(&b, "#zsh-win32-state 1\n");
    zbuf_str(&b, "@options ");
    zbuf_str(&b, opts_text);
    zbuf_str(&b, "\n@script\n");
    zbuf_str(&b, modules);
    zbuf_str(&b, aliases);
    zbuf_str(&b, functions);
    zbuf_str(&b, traps);
    zbuf_str(&b, params.buf);
    zbuf_str(&b, "\n");

    free(opts_text);
    free(aliases);
    free(functions);
    free(modules);
    free(traps);
    free(raw_params);
    zbuf_free(&params);
    return b.buf;
}

/* ------------------------------------------------------------------ */
/* Temporary files						      */
/* ------------------------------------------------------------------ */

/*
 * Create the name of a temporary file.  The file itself is not created.
 */
static char *
zsh_win32_tempname(const char *tag)
{
    char dir[MAX_PATH];
    char path[MAX_PATH];
    char *res;

    if (!GetTempPathA(sizeof(dir), dir))
	return NULL;
    if (!GetTempFileNameA(dir, tag, 0, path))
	return NULL;
    res = malloc(strlen(path) + 1);
    if (!res)
	return NULL;
    strcpy(res, path);
    /* GetTempFileName() creates the file; we want to write it ourselves. */
    DeleteFileA(path);
    return res;
}

/*
 * Write "text" to the file "name".  Returns 0 on success.
 */
static int
zsh_win32_write_file(const char *name, const char *text)
{
    int fd;
    size_t len = strlen(text);

    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
    if (fd < 0)
	return -1;
    while (len > 0) {
	int n = (int) write(fd, text, (unsigned int) len);
	if (n <= 0) {
	    close(fd);
	    return -1;
	}
	text += n;
	len -= (size_t) n;
    }
    close(fd);
    return 0;
}

/*
 * Read a whole file into a malloc()ed string.  Returns NULL if the file
 * cannot be read.
 */
static char *
zsh_win32_read_file(const char *name)
{
    int fd;
    struct zbuf b;
    char chunk[4096];

    fd = open(name, O_RDONLY | O_BINARY);
    if (fd < 0)
	return NULL;
    zbuf_init(&b);
    for (;;) {
	int n = (int) read(fd, chunk, (unsigned int) sizeof(chunk));
	if (n <= 0)
	    break;
	zbuf_add(&b, chunk, (size_t) n);
    }
    close(fd);
    if (!b.buf) {
	b.buf = malloc(1);
	if (b.buf)
	    b.buf[0] = '\0';
    }
    return b.buf;
}

/* ------------------------------------------------------------------ */
/* The helper processes						      */
/* ------------------------------------------------------------------ */

int zsh_win32_child_mode;
char *zsh_win32_child_statefile;
char *zsh_win32_child_cmdfile;
char **zsh_win32_multio_argv;

#define ZSH_WIN32_SUB_ARG	"--zsh-win32-subshell"
#define ZSH_WIN32_MULTIO_ARG	"--zsh-win32-multio"

/*
 * Remove the magic words from the argument vector.  Returns the mode
 * of this process.
 */
int
zsh_win32_child_start(char **argv)
{
    int i, j;

    if (!argv)
	return ZSH_WIN32_CHILD_NONE;
    for (i = 0; argv[i]; i++) {
	if (!strcmp(argv[i], ZSH_WIN32_SUB_ARG)) {
	    if (!argv[i + 1] || !argv[i + 2])
		break;
	    free(zsh_win32_child_statefile);
	    free(zsh_win32_child_cmdfile);
	    zsh_win32_child_statefile = ztrdup(argv[i + 1]);
	    zsh_win32_child_cmdfile = ztrdup(argv[i + 2]);
	    /* Remove the three words from the argument vector. */
	    for (j = i; argv[j + 3]; j++)
		argv[j] = argv[j + 3];
	    argv[j] = NULL;
	    zsh_win32_child_mode = ZSH_WIN32_CHILD_SUBSHELL;
	    return zsh_win32_child_mode;
	}
	if (!strcmp(argv[i], ZSH_WIN32_MULTIO_ARG)) {
	    /*
	     * The helper keeps its arguments: they describe the file
	     * descriptors it has to copy the data between.
	     */
	    zsh_win32_multio_argv = argv + i + 1;
	    zsh_win32_child_mode = ZSH_WIN32_CHILD_MULTIO;
	    return zsh_win32_child_mode;
	}
    }
    return ZSH_WIN32_CHILD_NONE;
}

/*
 * Restore the state the parent left behind and run the command we were
 * given.  Does not return.
 */
void
zsh_win32_child_run(void)
{
    char *state, *cmd;

    if (zsh_win32_child_mode == ZSH_WIN32_CHILD_MULTIO) {
	zsh_win32_multio_run();
	return;
    }

    state = zsh_win32_read_file(zsh_win32_child_statefile);
    cmd = zsh_win32_read_file(zsh_win32_child_cmdfile);
    remove(zsh_win32_child_statefile);
    remove(zsh_win32_child_cmdfile);

    if (getenv("ZSH_WIN32_DEBUG")) {
	fputs("=== state ===\n", stderr);
	fputs(state ? state : "(null)\n", stderr);
	fputs("=== cmd ===\n", stderr);
	fputs(cmd ? cmd : "(null)\n", stderr);
	fputs("=== end ===\n", stderr);
    }

    if (state) {
	char *opts_line, *script;

	/* The option state is applied directly, not by shell code. */
	opts_line = strstr(state, "@options ");
	if (opts_line) {
	    int i;
	    opts_line += 9;
	    for (i = 0; i < OPT_SIZE && opts_line[i * 2] && opts_line[i * 2 + 1];
		 i++) {
		char hex[3];
		int v;
		hex[0] = opts_line[i * 2];
		hex[1] = opts_line[i * 2 + 1];
		hex[2] = '\0';
		v = (int) strtol(hex, NULL, 16);
		opts[i] = (char) v;
	    }
	}

	/*
	 * A subshell is never interactive and does not do job control;
	 * the option state of the parent would otherwise switch both on.
	 */
	opts[INTERACTIVE] = 0;
	opts[MONITOR] = 0;

	script = strstr(state, "@script\n");
	if (script) {
	    script += 8;
	    execstring(script, 0, 0, "zsh-win32-state");
	}
	free(state);
    }

    /*
     * An EXIT trap must not run in a subshell, just as it does not run
     * in a forked child of a Unix shell.
     */
    sigtrapped[SIGEXIT] = 0;

    if (cmd) {
	execstring(cmd, 0, 1, "cmdsubst");
	free(cmd);
    }
    zexit(lastval, ZEXIT_NORMAL);
}

/* ------------------------------------------------------------------ */
/* The fork emulation						      */
/* ------------------------------------------------------------------ */

static HANDLE
zsh_win32_inheritable(HANDLE h)
{
    HANDLE nh = NULL;

    if (!h || h == INVALID_HANDLE_VALUE)
	return NULL;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &nh, 0,
			 TRUE, DUPLICATE_SAME_ACCESS))
	return NULL;
    return nh;
}

/*
 * Set up the attribute list that restricts the handles the child
 * inherits to the ones given in "handles".
 */
static LPPROC_THREAD_ATTRIBUTE_LIST
zsh_win32_attrlist(HANDLE *handles, int nhandles)
{
    LPPROC_THREAD_ATTRIBUTE_LIST attrlist = NULL;
    SIZE_T attrsize = 0;

    if (nhandles <= 0)
	return NULL;
    if (InitializeProcThreadAttributeList(NULL, 1, 0, &attrsize) || !attrsize)
	return NULL;
    attrlist = (LPPROC_THREAD_ATTRIBUTE_LIST)
	HeapAlloc(GetProcessHeap(), 0, attrsize);
    if (!attrlist)
	return NULL;
    if (!InitializeProcThreadAttributeList(attrlist, 1, 0, &attrsize)) {
	HeapFree(GetProcessHeap(), 0, attrlist);
	return NULL;
    }
    if (!UpdateProcThreadAttribute(attrlist, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				   handles, nhandles * sizeof(HANDLE), NULL,
				   NULL)) {
	DeleteProcThreadAttributeList(attrlist);
	HeapFree(GetProcessHeap(), 0, attrlist);
	return NULL;
    }
    return attrlist;
}

/*
 * Start a subshell running the command text "cmdtext".
 */
pid_t
zsh_win32_fork(const char *cmdtext, int infd, int outfd)
{
    char *state, *statefile = NULL, *cmdfile = NULL;
    char exe[MAX_PATH];
    char *cmdline;
    size_t cmdlen;
    STARTUPINFOEX siex;
    PROCESS_INFORMATION pi;
    LPPROC_THREAD_ATTRIBUTE_LIST attrlist;
    HANDLE inherit[3];
    HANDLE use[3];
    int i;
    BOOL ok;

    state = zsh_win32_build_state();
    if (!state)
	return -1;

    statefile = zsh_win32_tempname("zsh");
    cmdfile = zsh_win32_tempname("zsh");
    if (!statefile || !cmdfile) {
	free(state);
	free(statefile);
	free(cmdfile);
	return -1;
    }
    if (zsh_win32_write_file(statefile, state) < 0 ||
	zsh_win32_write_file(cmdfile, cmdtext ? cmdtext : "") < 0) {
	free(state);
	free(statefile);
	free(cmdfile);
	return -1;
    }
    free(state);

    if (!GetModuleFileNameA(NULL, exe, sizeof(exe))) {
	free(statefile);
	free(cmdfile);
	return -1;
    }

    cmdlen = strlen(exe) + strlen(statefile) + strlen(cmdfile) + 64;
    cmdline = malloc(cmdlen);
    if (!cmdline) {
	free(statefile);
	free(cmdfile);
	return -1;
    }
    snprintf(cmdline, cmdlen, "\"%s\" %s \"%s\" \"%s\"", exe,
	     ZSH_WIN32_SUB_ARG, statefile, cmdfile);

    /*
     * A descriptor of zero means "not redirected", exactly as in
     * execcmd_exec(); the child then uses the shell's own stream.
     */
    use[0] = (infd > 0) ? (HANDLE) _get_osfhandle(infd)
			: GetStdHandle(STD_INPUT_HANDLE);
    use[1] = (outfd > 0) ? (HANDLE) _get_osfhandle(outfd)
			 : GetStdHandle(STD_OUTPUT_HANDLE);
    use[2] = GetStdHandle(STD_ERROR_HANDLE);

    for (i = 0; i < 3; i++) {
	inherit[i] = zsh_win32_inheritable(use[i]);
	if (!inherit[i]) {
	    while (--i >= 0)
		CloseHandle(inherit[i]);
	    free(cmdline);
	    free(statefile);
	    free(cmdfile);
	    return -1;
	}
    }

    memset(&siex, 0, sizeof(siex));
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = inherit[0];
    siex.StartupInfo.hStdOutput = inherit[1];
    siex.StartupInfo.hStdError = inherit[2];

    /*
     * Only pass on the standard handles: the child must not inherit the
     * pipes and files the parent has open, or the parent would never
     * see the end of its output.
     */
    attrlist = zsh_win32_attrlist(inherit, 3);

    memset(&pi, 0, sizeof(pi));
    ok = CreateProcessA(exe, cmdline, NULL, NULL, TRUE,
			attrlist ? EXTENDED_STARTUPINFO_PRESENT : 0,
			NULL, NULL, &siex.StartupInfo, &pi);
    if (attrlist) {
	DeleteProcThreadAttributeList(attrlist);
	HeapFree(GetProcessHeap(), 0, attrlist);
    }
    free(cmdline);
    free(statefile);
    free(cmdfile);

    for (i = 0; i < 3; i++)
	CloseHandle(inherit[i]);

    if (!ok)
	return -1;

    CloseHandle(pi.hThread);
    zsh_win32_register_child((pid_t) pi.dwProcessId, pi.hProcess);
    return (pid_t) pi.dwProcessId;
}

/* ------------------------------------------------------------------ */
/* execve()								*/
/* ------------------------------------------------------------------ */

/*
 * Write one argument to a command line, quoted so that the Microsoft C
 * runtime of the child splits it back into a single argument.
 *
 * Arguments that need no quoting are copied unchanged: some programs
 * (notably cmd.exe) parse their own command line and would be confused
 * by quotes around simple words.  For the others the rules documented
 * for CommandLineToArgvW apply: the argument is enclosed in double
 * quotes, a run of backslashes that precedes a quote is doubled, and
 * backslashes at the end of the argument are doubled before the
 * closing quote.
 */
static char *
zsh_win32_quotearg(char *dst, const char *arg)
{
    const char *p = arg;

    if (*arg && !strpbrk(arg, " \t\n\v\"")) {
	while ((*dst = *p++))
	    dst++;
	return dst;
    }

    *dst++ = '"';
    for (;;) {
	size_t bs = 0, i;

	while (*p == '\\') {
	    bs++;
	    p++;
	}
	if (!*p) {
	    for (i = 0; i < bs * 2; i++)
		*dst++ = '\\';
	    break;
	}
	if (*p == '"') {
	    for (i = 0; i < bs * 2 + 1; i++)
		*dst++ = '\\';
	    *dst++ = *p++;
	} else {
	    for (i = 0; i < bs; i++)
		*dst++ = '\\';
	    *dst++ = *p++;
	}
    }
    *dst++ = '"';
    return dst;
}

/*
 * Replace this process with "path", like execve() on Unix.
 *
 * The C runtime's own execve() cannot be used: it builds the command
 * line without quoting, so an argument that contains a space (for
 * example "C:/Program Files/zsh/bin/zsh.exe") is split into several
 * arguments by the child.
 *
 * Since Windows cannot replace a process image, the program is started
 * as a child and this process waits for it, then exits with the same
 * status.  That is indistinguishable to the rest of the shell: the
 * caller never returns on success.
 */
int
zsh_win32_execve(const char *path, char *const argv[], char *const envp[])
{
    char *cmdline, *envblock = NULL, *p;
    char *arg0;
    size_t len = 0;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    HANDLE inherit[3];
    DWORD status, err;
    BOOL ok;
    int i;

    if (!path || !*path) {
	errno = ENOENT;
	return -1;
    }

    for (i = 0; argv && argv[i]; i++)
	len += 2 * strlen(argv[i]) + 4;
    if (!(cmdline = malloc(len + 1))) {
	errno = ENOMEM;
	return -1;
    }

    /*
     * Some programs, cmd.exe in particular, parse the command line
     * themselves and do not expect the program name to contain forward
     * slashes; give argv[0] in the native form.  Only the first
     * argument is touched, and only when it looks like a path.
     */
    arg0 = NULL;
    if (argv && argv[0] && strchr(argv[0], '/')) {
	size_t l = strlen(argv[0]), j;

	if ((arg0 = malloc(l + 1))) {
	    for (j = 0; j <= l; j++)
		arg0[j] = argv[0][j] == '/' ? '\\' : argv[0][j];
	}
    }

    p = cmdline;
    for (i = 0; argv && argv[i]; i++) {
	p = zsh_win32_quotearg(p, (i == 0 && arg0) ? arg0 : argv[i]);
	*p++ = ' ';
    }
    if (p > cmdline)
	p--;
    *p = '\0';
    free(arg0);

    if (envp) {
	size_t elen = 0;

	for (i = 0; envp[i]; i++)
	    elen += strlen(envp[i]) + 1;
	if (!(envblock = malloc(elen + 1))) {
	    free(cmdline);
	    errno = ENOMEM;
	    return -1;
	}
	p = envblock;
	for (i = 0; envp[i]; i++) {
	    size_t l = strlen(envp[i]) + 1;

	    memcpy(p, envp[i], l);
	    p += l;
	}
	*p = '\0';
    }

    /*
     * Pass on the standard handles, exactly as the fork emulation does,
     * so that redirections made by the shell apply to the new process.
     */
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    for (i = 0; i < 3; i++)
	inherit[i] = zsh_win32_inheritable((HANDLE) _get_osfhandle(i));
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inherit[0];
    si.hStdOutput = inherit[1];
    si.hStdError = inherit[2];

    memset(&pi, 0, sizeof(pi));
    ok = CreateProcessA(path, cmdline, NULL, NULL, TRUE, 0, envblock, NULL,
			&si, &pi);

    for (i = 0; i < 3; i++)
	if (inherit[i])
	    CloseHandle(inherit[i]);
    free(cmdline);
    free(envblock);

    if (!ok) {
	err = GetLastError();
	if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
	    errno = ENOENT;
	else if (err == ERROR_ACCESS_DENIED)
	    errno = EACCES;
	else if (err == ERROR_BAD_EXE_FORMAT || err == ERROR_BAD_FORMAT)
	    errno = ENOEXEC;
	else
	    errno = ENOENT;
	return -1;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &status))
	status = 1;
    CloseHandle(pi.hProcess);

    /*
     * The child's exit status is the command's status; a value with the
     * signal bit set is decoded by zsh_win32_child_status().
     */
    _exit((int) status);
}

/* ------------------------------------------------------------------ */
/* The helper that copies data for multiple redirections	      */
/* ------------------------------------------------------------------ */

/*
 * Copy "len" bytes to the descriptor "fd", retrying on partial writes.
 */
static int
zsh_win32_write_all(int fd, const char *buf, int len)
{
    while (len > 0) {
	int n = (int) write(fd, buf, (unsigned int) len);
	if (n <= 0)
	    return -1;
	buf += n;
	len -= n;
    }
    return 0;
}

/*
 * The child side of the multiple redirection helper.  The arguments
 * are the flag saying whether this is the "tee" case, the number of
 * files and then the handles of the pipe and of the files.
 */
void
zsh_win32_multio_run(void)
{
    char **argv;
    int rflag, nfds, i;
    int pipefd = -1;
    int *fds;
    char buf[4092];
    int len;

    argv = zsh_win32_multio_argv;
    if (!argv) {
	ExitProcess(1);
	return;
    }
    rflag = atoi(argv[0]);
    nfds = atoi(argv[1]);
    if (nfds < 0)
	nfds = 0;
    pipefd = _open_osfhandle((intptr_t) argv[2],
			     O_BINARY | (rflag ? O_RDONLY : O_WRONLY));
    fds = malloc(sizeof(int) * (nfds > 0 ? nfds : 1));
    if (!fds)
	ExitProcess(1);
    for (i = 0; i < nfds; i++)
	fds[i] = _open_osfhandle((intptr_t) argv[3 + i],
				 O_BINARY | (rflag ? O_WRONLY : O_RDONLY));
    if (pipefd < 0)
	ExitProcess(1);

    if (rflag) {
	/* tee process: the pipe is the input */
	while ((len = (int) read(pipefd, buf, sizeof(buf))) != 0) {
	    if (len < 0) {
		if (errno == EINTR)
		    continue;
		break;
	    }
	    for (i = 0; i < nfds; i++)
		if (zsh_win32_write_all(fds[i], buf, len) < 0)
		    break;
	}
    } else {
	/* cat process: the pipe is the output */
	for (i = 0; i < nfds; i++)
	    while ((len = (int) read(fds[i], buf, sizeof(buf))) != 0) {
		if (len < 0) {
		    if (errno == EINTR)
			continue;
		    break;
		}
		if (zsh_win32_write_all(pipefd, buf, len) < 0)
		    break;
	    }
    }
    ExitProcess(0);
}

/*
 * Start the helper process that copies the data.
 */
pid_t
zsh_win32_multio(int rflag, int pipefd, int *fds, int nfds)
{
    char exe[MAX_PATH];
    char *cmdline;
    size_t cmdlen, used;
    STARTUPINFOEX siex;
    PROCESS_INFORMATION pi;
    LPPROC_THREAD_ATTRIBUTE_LIST attrlist;
    HANDLE inherit[32];
    HANDLE nul;
    int nhandles = 0;
    int i;
    BOOL ok;

    if (nfds < 0 || nfds > 30)
	return -1;

    if (!GetModuleFileNameA(NULL, exe, sizeof(exe)))
	return -1;

    nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
		      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		      0, NULL);
    if (nul == INVALID_HANDLE_VALUE)
	nul = NULL;

    /* The pipe comes first, then the files. */
    inherit[nhandles++] = zsh_win32_inheritable((HANDLE) _get_osfhandle(pipefd));
    for (i = 0; i < nfds; i++)
	inherit[nhandles++] = zsh_win32_inheritable((HANDLE) _get_osfhandle(fds[i]));
    for (i = 0; i < nhandles; i++) {
	if (!inherit[i]) {
	    while (--i >= 0)
		CloseHandle(inherit[i]);
	    if (nul)
		CloseHandle(nul);
	    return -1;
	}
    }

    cmdlen = strlen(exe) + 128 + (size_t) nhandles * 16;
    cmdline = malloc(cmdlen);
    if (!cmdline) {
	for (i = 0; i < nhandles; i++)
	    CloseHandle(inherit[i]);
	if (nul)
	    CloseHandle(nul);
	return -1;
    }
    used = (size_t) snprintf(cmdline, cmdlen, "\"%s\" %s %d %d %ld", exe,
			     ZSH_WIN32_MULTIO_ARG, rflag ? 1 : 0, nfds,
			     (long) (intptr_t) inherit[0]);
    for (i = 1; i < nhandles; i++)
	used += (size_t) snprintf(cmdline + used, cmdlen - used, " %ld",
				   (long) (intptr_t) inherit[i]);

    memset(&siex, 0, sizeof(siex));
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = nul ? nul : inherit[0];
    siex.StartupInfo.hStdOutput = nul ? nul : inherit[0];
    siex.StartupInfo.hStdError = nul ? nul : inherit[0];

    attrlist = zsh_win32_attrlist(inherit, nhandles);

    memset(&pi, 0, sizeof(pi));
    ok = CreateProcessA(exe, cmdline, NULL, NULL, TRUE,
			attrlist ? EXTENDED_STARTUPINFO_PRESENT : 0,
			NULL, NULL, &siex.StartupInfo, &pi);
    if (attrlist) {
	DeleteProcThreadAttributeList(attrlist);
	HeapFree(GetProcessHeap(), 0, attrlist);
    }
    free(cmdline);
    for (i = 0; i < nhandles; i++)
	CloseHandle(inherit[i]);
    if (nul)
	CloseHandle(nul);

    if (!ok)
	return -1;

    CloseHandle(pi.hThread);
    zsh_win32_register_child((pid_t) pi.dwProcessId, pi.hProcess);
    return (pid_t) pi.dwProcessId;
}
