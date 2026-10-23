/*
 * term.c - terminal handling for the native Win32 port of zsh
 *
 * zsh expects a Unix style terminal: it puts the terminal into raw mode
 * for the line editor and reads escape sequences itself.  Windows
 * consoles can do this as well, provided that virtual terminal processing
 * and virtual terminal input are switched on.
 */

#include "win32.h"

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Console helpers						      */
/* ------------------------------------------------------------------ */

/**/
int
zsh_win32_fd_is_console(int fd)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD type;

    if (h == INVALID_HANDLE_VALUE || h == NULL)
	return 0;
    return GetConsoleMode(h, &type) ? 1 : 0;
}

/*
 * Is this running under Wine?  Wine exports wine_get_version() from
 * ntdll.dll; no real Windows system does.
 */
/**/
int
zsh_win32_is_wine(void)
{
    static int cached = -1;

    if (cached < 0) {
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");

	cached = (ntdll && GetProcAddress(ntdll, "wine_get_version")) ? 1 : 0;
    }
    return cached;
}

/*
 * Wine's console does not interpret the escape sequences that the line
 * editor sends to it: it counts every single byte of them as a column.
 * The count therefore runs ahead of the real cursor position, and once it
 * reaches the width of the console buffer Wine wraps the line itself and
 * injects a newline into the output stream.  That newline lands in the
 * middle of the line the user is editing and the display falls apart.
 *
 * The real console of Windows understands the escape sequences and keeps
 * its cursor position in step, so nothing has to be done there.  Under
 * Wine the buffer is made much wider than the visible window instead:
 * the miscount then needs thousands of bytes before it can reach the
 * margin, and the buffer is reset to the window width at every new
 * prompt, which is where the line editor starts drawing again.
 */
/**/
static void
zsh_win32_widen_console_buffer(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE h = NULL;
    COORD size;
    int fd;

    /* The same preference as for the output file descriptor: stderr. */
    for (fd = 2; fd >= 1; fd--) {
	HANDLE cand = (HANDLE) _get_osfhandle(fd);

	if (cand != INVALID_HANDLE_VALUE && cand != NULL &&
	    GetConsoleScreenBufferInfo(cand, &info)) {
	    h = cand;
	    break;
	}
    }
    if (!h)
	return;
    if (info.dwSize.X >= ZSH_WIN32_WIDE_CONSOLE)
	return;
    size.X = ZSH_WIN32_WIDE_CONSOLE;
    size.Y = info.dwSize.Y;
    if (size.Y < info.srWindow.Bottom - info.srWindow.Top + 1)
	size.Y = info.srWindow.Bottom - info.srWindow.Top + 1;
    SetConsoleScreenBufferSize(h, size);
}

/*
 * A Windows console has separate input (CONIN$) and output (CONOUT$)
 * handles, but zsh's terminal descriptor is bidirectional.  SHTTY is
 * the input handle: that is what zle reads from and what tcsetattr()
 * configures.  Anything that writes to the terminal therefore has to
 * be aimed at a console output handle instead.
 *
 * Return a file descriptor for console output, or -1 if there is no
 * console.  The caller owns the descriptor.
 */
int
zsh_win32_console_output_fd(void)
{
    int fd;
    HANDLE h;
    DWORD mode;
    CONSOLE_SCREEN_BUFFER_INFO info;

    /*
     * Prefer the standard error stream: for an interactive shell it is
     * the terminal, and it already has virtual terminal processing
     * enabled by zsh_win32_init_console().
     */
    for (fd = 2; fd >= 1; fd--) {
	h = (HANDLE) _get_osfhandle(fd);
	if (h != INVALID_HANDLE_VALUE && h != NULL &&
	    GetConsoleScreenBufferInfo(h, &info)) {
	    int dupfd = _dup(fd);
	    if (dupfd >= 0)
		return dupfd;
	}
    }
    /*
     * stdout and stderr are redirected, but we still have a console:
     * open its output side directly.
     */
    fd = _open("CONOUT$", _O_WRONLY | _O_BINARY);
    if (fd >= 0) {
	h = (HANDLE) _get_osfhandle(fd);
	if (h != INVALID_HANDLE_VALUE && h != NULL &&
	    GetConsoleMode(h, &mode))
	    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return fd;
}

/*
 * Write to a terminal the way zsh expects to be able to.  If "fd" is a
 * console input handle (as SHTTY is), the bytes are sent to the console
 * output instead; otherwise this is an ordinary write.
 */
int
zsh_win32_console_write(int fd, const void *buf, unsigned int n)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (h != INVALID_HANDLE_VALUE && h != NULL &&
	!GetConsoleScreenBufferInfo(h, &info) &&
	GetConsoleMode(h, &(DWORD){ 0 })) {
	int outfd = zsh_win32_console_output_fd();
	if (outfd >= 0) {
	    /* If this write starts a prompt (the OSC 133;P;k=i marker),
	     * and the cursor is not at the start of a line, move to the
	     * next line first so the prompt does not overwrite the
	     * previous command's output. */
	    if (n >= 14) {
		const char *needle = "\033]133;A;";
		int ni;
		for (ni = 0; ni + 9 <= (int) n; ni++)
		    if (memcmp((const char *) buf + ni, needle, 9) == 0)
			break;
		if (ni + 9 <= (int) n) {
		    CONSOLE_SCREEN_BUFFER_INFO ci;
		    HANDLE oh = GetStdHandle(STD_OUTPUT_HANDLE);
		    if (oh != INVALID_HANDLE_VALUE && oh != NULL &&
			GetConsoleScreenBufferInfo(oh, &ci) &&
			ci.dwCursorPosition.X != 0) {
			_write(outfd, "\r\n", 2);
		    }
		}
	    }
	    {
		int ret = _write(outfd, buf, n);
		int saved = errno;
		_close(outfd);
		errno = saved;
		return ret;
	    }
	}
    }
    return _write(fd, buf, n);
}

/* Current cursor position according to the console, for diagnostics. */

/*
 * Return non-zero if the console input buffer holds at least one
 * character.  Merely waiting for the console handle to become signalled
 * is not enough: modifier keys produce input events without characters.
 */
static int
zsh_console_input_ready(HANDLE h)
{
    DWORD n = 0, i;
    INPUT_RECORD *recs;
    int ready = 0;

    if (!GetNumberOfConsoleInputEvents(h, &n) || n == 0)
	return 0;
    {
	/*
	 * In raw mode every key event matters, including the ones that
	 * carry no character (escape, cursor keys, modifiers).
	 */
	DWORD mode;

	if (GetConsoleMode(h, &mode) && !(mode & ENABLE_LINE_INPUT))
	    return 1;
    }
    recs = malloc(n * sizeof(INPUT_RECORD));
    if (!recs)
	return 1;
    /*
     * Peek, do not read: the records have to stay in the console input
     * buffer for zsh_win32_read() to pick up.
     */
    if (PeekConsoleInputW(h, recs, n, &n)) {
	for (i = 0; i < n; i++)
	    if (recs[i].EventType == KEY_EVENT &&
		recs[i].Event.KeyEvent.uChar.AsciiChar != 0) {
		ready = 1;
		break;
	    }
    }
    free(recs);
    return ready;
}

static int zsh_console_cps_saved;
static UINT zsh_old_out_cp, zsh_old_in_cp;

static void
zsh_win32_restore_console_cp(void)
{
    if (!zsh_console_cps_saved)
	return;
    SetConsoleOutputCP(zsh_old_out_cp);
    SetConsoleCP(zsh_old_in_cp);
    zsh_console_cps_saved = 0;
}

/**/
void
zsh_win32_init_console(void)
{
    int fd;
    HANDLE hin = (HANDLE) _get_osfhandle(0);
    HANDLE hout = (HANDLE) _get_osfhandle(1);

    for (fd = 1; fd <= 2; fd++) {
	HANDLE h = (HANDLE) _get_osfhandle(fd);
	DWORD mode;

	if (h == INVALID_HANDLE_VALUE || h == NULL)
	    continue;
	if (!GetConsoleMode(h, &mode))
	    continue;
	SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    /*
     * The shell speaks UTF-8, so a console has to use that code page for
     * both directions.  The previous settings are restored when the
     * shell exits.
     */
    if (hout != INVALID_HANDLE_VALUE && hout != NULL &&
	GetConsoleMode(hout, &(DWORD){ 0 })) {
	if (!zsh_console_cps_saved) {
	    zsh_old_out_cp = GetConsoleOutputCP();
	    zsh_old_in_cp = GetConsoleCP();
	    zsh_console_cps_saved = 1;
	    atexit(zsh_win32_restore_console_cp);
	}
	SetConsoleOutputCP(CP_UTF8);
    }
    if (hin != INVALID_HANDLE_VALUE && hin != NULL &&
	GetConsoleMode(hin, &(DWORD){ 0 })) {
	if (!zsh_console_cps_saved) {
	    zsh_old_out_cp = GetConsoleOutputCP();
	    zsh_old_in_cp = GetConsoleCP();
	    zsh_console_cps_saved = 1;
	    atexit(zsh_win32_restore_console_cp);
	}
	SetConsoleCP(CP_UTF8);
    }
    /*
     * The standard streams are set to binary mode when they are not
     * consoles, so that the shell does not translate line endings:
     * Unix-like output is what scripts expect.
     */
    for (fd = 0; fd <= 2; fd++) {
	HANDLE h = (HANDLE) _get_osfhandle(fd);

	if (h == INVALID_HANDLE_VALUE || h == NULL)
	    continue;
	if (!GetConsoleMode(h, &(DWORD){ 0 }))
	    _setmode(fd, _O_BINARY);
    }

    if (zsh_win32_is_wine())
	zsh_win32_widen_console_buffer();
}

/* ------------------------------------------------------------------ */
/* Reading							      */
/* ------------------------------------------------------------------ */

/*
 * Console input is read here and buffered, rather than through the C
 * library.  The line editor needs to know whether a byte is already
 * waiting before it blocks: once the C library has pulled a whole run of
 * characters into its own buffer, asking the console whether input is
 * available no longer tells the truth, and escape sequences such as the
 * arrow keys would be broken up.
 */
static char console_inbuf[4096];
static int console_inbuf_len;
static int console_inbuf_pos;

/* Is there a character waiting for us? */
static int
zsh_console_pending(HANDLE h)
{
    if (console_inbuf_pos < console_inbuf_len)
	return 1;
    return zsh_console_input_ready(h);
}

/*
 * Convert the wide characters the console gave us into UTF-8 and stash
 * them in the input buffer.  Returns 1 on success.
 */
static int
zsh_console_fill_cooked(HANDLE h)
{
    wchar_t wbuf[512];
    DWORD got = 0;
    int i, len = 0;

    if (!ReadConsoleW(h, wbuf, (DWORD) (sizeof(wbuf) / sizeof(wbuf[0])),
		      &got, NULL) || got == 0)
	return 0;
    for (i = 0; i < (int) got; i++) {
	char utf8[8];
	int n = WideCharToMultiByte(CP_UTF8, 0, wbuf + i, 1, utf8,
				    sizeof(utf8), NULL, NULL);

	if (n > 0) {
	    if (len + n > (int) sizeof(console_inbuf))
		break;
	    memcpy(console_inbuf + len, utf8, n);
	    len += n;
	}
    }
    console_inbuf_len = len;
    console_inbuf_pos = 0;
    return len > 0;
}

/* Append the UTF-8 form of a wide character. */
static int
zsh_console_putwc(wchar_t wc, int len)
{
    char utf8[8];
    int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8),
				NULL, NULL);

    if (n <= 0 || len + n > (int) sizeof(console_inbuf))
	return len;
    memcpy(console_inbuf + len, utf8, n);
    return len + n;
}

/*
 * The escape sequences the line editor expects for the keys that do not
 * produce a character.  A Windows console reports these as virtual key
 * codes, so they have to be translated by hand.
 */
static const struct {
    WORD vk;
    const char *seq;
} zsh_console_keys[] = {
    { VK_UP,		"\033[A" },
    { VK_DOWN,		"\033[B" },
    { VK_RIGHT,		"\033[C" },
    { VK_LEFT,		"\033[D" },
    { VK_HOME,		"\033[H" },
    { VK_END,		"\033[F" },
    { VK_INSERT,	"\033[2~" },
    { VK_DELETE,	"\033[3~" },
    { VK_PRIOR,		"\033[5~" },
    { VK_NEXT,		"\033[6~" },
    { VK_F1,		"\033[11~" },
    { VK_F2,		"\033[12~" },
    { VK_F3,		"\033[13~" },
    { VK_F4,		"\033[14~" },
    { VK_F5,		"\033[15~" },
    { VK_F6,		"\033[17~" },
    { VK_F7,		"\033[18~" },
    { VK_F8,		"\033[19~" },
    { VK_F9,		"\033[20~" },
    { VK_F10,		"\033[21~" },
    { VK_F11,		"\033[23~" },
    { VK_F12,		"\033[24~" },
    { VK_BACK,		"\177" },
    { VK_TAB,		"\t" },
    { VK_RETURN,	"\r" },
    { VK_ESCAPE,	"\033" },
    { 0, NULL }
};

/*
 * Raw mode: the console hands out key events one at a time.  Translate
 * them into the byte stream a terminal would produce.
 */
static int
zsh_console_fill_raw(HANDLE h)
{
    INPUT_RECORD recs[64];
    DWORD got = 0, i;
    int len = 0;

    if (!ReadConsoleInputW(h, recs, 64, &got) || got == 0)
	return 0;
    for (i = 0; i < got; i++) {
	KEY_EVENT_RECORD *k;
	int j;

	if (recs[i].EventType != KEY_EVENT)
	    continue;
	k = &recs[i].Event.KeyEvent;
	if (!k->bKeyDown)
	    continue;
	if (k->uChar.UnicodeChar) {
	    len = zsh_console_putwc(k->uChar.UnicodeChar, len);
	    continue;
	}
	/*
	 * A bare escape arrives as an Alt key press on some console
	 * implementations (wine does this with a Unix terminal behind it);
	 * elsewhere it is VK_ESCAPE, which is in the table below.
	 */
	if (k->wVirtualKeyCode == VK_MENU) {
	    if (len < (int) sizeof(console_inbuf))
		console_inbuf[len++] = '\033';
	    continue;
	}
	for (j = 0; zsh_console_keys[j].seq; j++)
	    if (zsh_console_keys[j].vk == k->wVirtualKeyCode) {
		int slen = (int) strlen(zsh_console_keys[j].seq);

		if (len + slen <= (int) sizeof(console_inbuf)) {
		    memcpy(console_inbuf + len, zsh_console_keys[j].seq, slen);
		    len += slen;
		}
		break;
	    }
    }
    console_inbuf_len = len;
    console_inbuf_pos = 0;
    return len > 0;
}

/**/
static int
zsh_console_fill(HANDLE h)
{
    DWORD mode;

    if (GetConsoleMode(h, &mode) && !(mode & ENABLE_LINE_INPUT))
	return zsh_console_fill_raw(h);
    return zsh_console_fill_cooked(h);
}

/**/
int
zsh_win32_read(int fd, void *buf, unsigned int n)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD mode;
    int avail, want;

    if (h == INVALID_HANDLE_VALUE || h == NULL)
	return _read(fd, buf, n);
    if (!GetConsoleMode(h, &mode))
	return _read(fd, buf, n);

    /*
     * Console input: wait until there really is something to read, so
     * that a pending signal can be delivered in the meantime.
     */
    while (console_inbuf_pos >= console_inbuf_len) {
	if (zsh_win32_poll_signals()) {
	    errno = EINTR;
	    return -1;
	}
	if (zsh_console_input_ready(h)) {
	    if (zsh_console_fill(h))
		break;
	    /* Only modifier keys; wait for something usable. */
	}
	Sleep(10);
    }

    avail = console_inbuf_len - console_inbuf_pos;
    want = (int) n < avail ? (int) n : avail;
    memcpy(buf, console_inbuf + console_inbuf_pos, want);
    console_inbuf_pos += want;

    /*
     * The console ends a line with CR LF; the shell expects a plain
     * newline.  This only ever happens in cooked mode, where the pair
     * really does belong together.
     */
    if (want > 0) {
	char *p = buf;
	int i, j = 0;

	for (i = 0; i < want; i++) {
	    if (p[i] == '\r' && i + 1 < want && p[i + 1] == '\n')
		continue;
	    p[j++] = p[i];
	}
	want = j;
    }
    return want;
}

/* ------------------------------------------------------------------ */
/* select()							      */
/* ------------------------------------------------------------------ */

/*
 * Is there something to read on this descriptor?
 */
static int
zsh_fd_readable(int fd)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD type;

    if (h == INVALID_HANDLE_VALUE || h == NULL)
	return 1;			/* regular file: always ready */

    if (GetConsoleMode(h, &type))
	return zsh_console_pending(h);

    {
	DWORD type2 = GetFileType(h), avail = 0;

	if (type2 == FILE_TYPE_PIPE) {
	    if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
		return avail > 0;
	    /* A broken pipe means end of file: ready. */
	    return 1;
	}
	if (type2 == FILE_TYPE_CHAR)
	    return zsh_console_pending(h);
	if (type2 == FILE_TYPE_DISK)
	    return 1;
    }
    return 1;
}

/**/
static int
zsh_fd_writable(int fd)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD type;

    if (h == INVALID_HANDLE_VALUE || h == NULL)
	return 1;
    if (GetConsoleMode(h, &type))
	return 1;
    if (GetFileType(h) == FILE_TYPE_PIPE) {
	DWORD avail = 0;
	if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
	    return 1;
	return 0;		/* the read end is closed */
    }
    return 1;
}

/**/
int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
       struct timeval *timeout)
{
    fd_set r, w, e;
    long deadline = -1;
    int i;

    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_ZERO(&e);
    if (readfds)
	r = *readfds;
    if (writefds)
	w = *writefds;
    if (exceptfds)
	e = *exceptfds;

    if (timeout) {
	long ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

	deadline = GetTickCount() + ms;
    }

    for (;;) {
	int nready = 0;

	if (zsh_win32_poll_signals()) {
	    errno = EINTR;
	    return -1;
	}
	for (i = 0; i < nfds; i++) {
	    if (FD_ISSET(i, &r) && zsh_fd_readable(i)) {
		FD_SET(i, &r);
		nready++;
	    } else if (FD_ISSET(i, &r))
		FD_CLR(i, &r);
	    if (FD_ISSET(i, &w) && zsh_fd_writable(i)) {
		FD_SET(i, &w);
		nready++;
	    } else if (FD_ISSET(i, &w))
		FD_CLR(i, &w);
	    FD_CLR(i, &e);
	}
	if (nready) {
	    if (readfds)
		*readfds = r;
	    if (writefds)
		*writefds = w;
	    if (exceptfds)
		*exceptfds = e;
	    return nready;
	}
	if (deadline >= 0 && (long) (GetTickCount() - deadline) >= 0) {
	    if (readfds)
		*readfds = r;
	    if (writefds)
		*writefds = w;
	    if (exceptfds)
		*exceptfds = e;
	    return 0;
	}
	Sleep(10);
    }
}

/* ------------------------------------------------------------------ */
/* Terminal attributes						      */
/* ------------------------------------------------------------------ */

/**/
static void
zsh_default_termios(struct termios *t)
{
    memset(t, 0, sizeof(*t));
    t->c_iflag = ICRNL | IXON;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD | CLOCAL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    t->c_cc[VINTR] = 3;
    t->c_cc[VQUIT] = 28;
    t->c_cc[VERASE] = 127;
    t->c_cc[VKILL] = 21;
    t->c_cc[VEOF] = 4;
    t->c_cc[VTIME] = 0;
    t->c_cc[VMIN] = 1;
    t->c_cc[VSTART] = 17;
    t->c_cc[VSTOP] = 19;
    t->c_cc[VSUSP] = 26;
    t->c_ispeed = B9600;
    t->c_ospeed = B9600;
}

/**/
int
tcgetattr(int fd, struct termios *t)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD mode;

    if (!t) {
	errno = EINVAL;
	return -1;
    }
    if (h == INVALID_HANDLE_VALUE || h == NULL || !GetConsoleMode(h, &mode)) {
	errno = ENOTTY;
	return -1;
    }
    zsh_default_termios(t);
    if (!(mode & ENABLE_LINE_INPUT))
	t->c_lflag &= ~ICANON;
    if (!(mode & ENABLE_ECHO_INPUT))
	t->c_lflag &= ~ECHO;
    if (!(mode & ENABLE_PROCESSED_INPUT))
	t->c_lflag &= ~ISIG;
    return 0;
}

/**/
int
tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD mode;

    UNUSED_ARG(optional_actions);
    if (!t) {
	errno = EINVAL;
	return -1;
    }
    if (h == INVALID_HANDLE_VALUE || h == NULL || !GetConsoleMode(h, &mode)) {
	errno = ENOTTY;
	return -1;
    }
    if (t->c_lflag & ICANON)
	mode |= ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    else
	mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    if (t->c_lflag & ISIG)
	mode |= ENABLE_PROCESSED_INPUT;
    else
	mode &= ~ENABLE_PROCESSED_INPUT;
    /*
     * Disable VT input: the port reads key events via ReadConsoleInputW and
     * maps VK codes to escape sequences itself.  Leaving VT input on makes
     * the console deliver arrow keys etc. as text, which confuses our reader.
     */
    mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(h, mode)) {
	errno = ENOTTY;
	return -1;
    }
    return 0;
}

/**/
int
tcdrain(int fd)
{
    UNUSED_ARG(fd);
    return 0;
}

/**/
int
tcflush(int fd, int queue_selector)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD mode;

    UNUSED_ARG(queue_selector);
    if (h == INVALID_HANDLE_VALUE || h == NULL || !GetConsoleMode(h, &mode))
	return 0;
    FlushConsoleInputBuffer(h);
    return 0;
}

/**/
pid_t
tcgetpid(int fd)
{
    UNUSED_ARG(fd);
    return getpid();
}

/**/
int
tcgetpgrp(int fd)
{
    UNUSED_ARG(fd);
    return (int) getpid();
}

/**/
int
tcsetpgrp(int fd, pid_t pgid)
{
    UNUSED_ARG(fd);
    UNUSED_ARG(pgid);
    /* Windows has no process groups for the console. */
    return 0;
}

/**/
speed_t
cfgetispeed(const struct termios *t)
{
    return t ? t->c_ispeed : B9600;
}

/**/
speed_t
cfgetospeed(const struct termios *t)
{
    return t ? t->c_ospeed : B9600;
}

/**/
int
cfsetispeed(struct termios *t, speed_t speed)
{
    if (!t)
	return -1;
    t->c_ispeed = speed;
    return 0;
}

/**/
int
cfsetospeed(struct termios *t, speed_t speed)
{
    if (!t)
	return -1;
    t->c_ospeed = speed;
    return 0;
}

/**/
void
cfmakeraw(struct termios *t)
{
    if (!t)
	return;
    t->c_iflag = 0;
    t->c_oflag = 0;
    t->c_cflag = CS8;
    t->c_lflag = 0;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

/* ------------------------------------------------------------------ */
/* ioctl()							      */
/* ------------------------------------------------------------------ */

/**/
int
ioctl(int fd, unsigned long request, void *arg)
{
    HANDLE h = (HANDLE) _get_osfhandle(fd);

    switch (request) {
    case TIOCGWINSZ:
	{
	    CONSOLE_SCREEN_BUFFER_INFO csbi;

	    if (!GetConsoleScreenBufferInfo(h, &csbi)) {
		errno = ENOTTY;
		return -1;
	    }
	    if (arg) {
		struct winsize *ws = arg;

		ws->ws_row = (unsigned short) (csbi.srWindow.Bottom -
					       csbi.srWindow.Top + 1);
		ws->ws_col = (unsigned short) (csbi.srWindow.Right -
					       csbi.srWindow.Left + 1);
		ws->ws_xpixel = 0;
		ws->ws_ypixel = 0;
	    }
	    return 0;
	}
    case TIOCSWINSZ:
	return 0;
    case FIONREAD:
	{
	    DWORD avail = 0;

	    if (arg && GetConsoleMode(h, &(DWORD){ 0 })) {
		INPUT_RECORD *recs;
		DWORD n = 0, i;
		int count = 0;

		if (!GetNumberOfConsoleInputEvents(h, &n) || n == 0) {
		    *(int *) arg = 0;
		    return 0;
		}
		recs = malloc(n * sizeof(INPUT_RECORD));
		if (recs && ReadConsoleInputW(h, recs, n, &n)) {
		    for (i = 0; i < n; i++)
			if (recs[i].EventType == KEY_EVENT)
			    count++;
		    free(recs);
		}
		*(int *) arg = count;
		return 0;
	    }
	    if (arg && GetFileType(h) == FILE_TYPE_PIPE) {
		if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
		    avail = 0;
		*(int *) arg = (int) avail;
		return 0;
	    }
	    if (arg)
		*(int *) arg = 0;
	    return 0;
	}
    case TIOCGPGRP:
	if (arg)
	    *(int *) arg = (int) getpid();
	return 0;
    case TIOCSPGRP:
    case TIOCSETD:
    case TIOCNXCL:
	return 0;
    default:
	errno = ENOTTY;
	return -1;
    }
}
