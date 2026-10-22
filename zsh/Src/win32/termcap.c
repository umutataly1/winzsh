/*
 * termcap.c - a small terminal capability database for Windows
 *
 * Windows has no termcap database, but the shell needs a handful of
 * capabilities to draw its prompt and to run the line editor.  This
 * file provides just enough of the traditional interface for the shell
 * to work: the capabilities are expressed with the escape sequences of
 * a virtual terminal, which the console of Windows 10 and later
 * understands when it runs with virtual terminal processing enabled.
 *
 * The capabilities that are not listed here simply do not exist, which
 * is what the shell expects when a terminal does not have them.
 */

#include "win32.h"

/* The capability names the shell asks for, and their values. */

struct zsh_cap {
    const char *name;
    const char *value;
};

static const struct zsh_cap zsh_caps[] = {
    /* cl: clear the screen and home the cursor */
    { "cl",	"\033[2J\033[H" },
    /* le: move the cursor one column to the left */
    { "le",	"\033[D" },
    /* LE: move the cursor several columns to the left */
    { "LE",	"\033[%p1%dD" },
    /* nd: move the cursor one column to the right */
    { "nd",	"\033[C" },
    /* RI: move the cursor several columns to the right */
    { "RI",	"\033[%p1%dC" },
    /* up: move the cursor up one line */
    { "up",	"\033[A" },
    /* UP: move the cursor up */
    { "UP",	"\033[%p1%dA" },
    /* do: move the cursor down one line */
    { "do",	"\033[B" },
    /* DO: move the cursor down */
    { "DO",	"\033[%p1%dB" },
    /* dc: delete a character */
    { "dc",	"\033[P" },
    /* DC: delete characters */
    { "DC",	"\033[%p1%dP" },
    /* ic: insert a character */
    { "ic",	"\033[@" },
    /* IC: insert characters */
    { "IC",	"\033[%p1%d@" },
    /* cd: clear to the end of the screen */
    { "cd",	"\033[J" },
    /* ce: clear to the end of the line */
    { "ce",	"\033[K" },
    /* al: insert a line */
    { "al",	"\033[L" },
    /* dl: delete a line */
    { "dl",	"\033[M" },
    /* ta: move to the next tab stop */
    { "ta",	"\t" },
    /* md: bold */
    { "md",	"\033[1m" },
    /* mh: faint */
    { "mh",	"\033[2m" },
    /* so: enter standout mode */
    { "so",	"\033[7m" },
    /* us: enter underline mode */
    { "us",	"\033[4m" },
    /* ZH: enter italics mode */
    { "ZH",	"\033[3m" },
    /* me: leave standout and underline mode */
    { "me",	"\033[0m" },
    /* se: leave standout mode */
    { "se",	"\033[27m" },
    /* ue: leave underline mode */
    { "ue",	"\033[24m" },
    /* ZR: leave italics mode */
    { "ZR",	"\033[23m" },
    /* ch: move to an absolute column */
    { "ch",	"\033[%p1%dG" },
    /* ku: cursor up */
    { "ku",	"\033[A" },
    /* kd: cursor down */
    { "kd",	"\033[B" },
    /* kl: cursor left */
    { "kl",	"\033[D" },
    /* kr: cursor right */
    { "kr",	"\033[C" },
    /* sc: save the cursor position */
    { "sc",	"\033[s" },
    /* rc: restore the cursor position */
    { "rc",	"\033[u" },
    /* bc: backspace */
    { "bc",	"\b" },
    /* AF: begin a shifted key */
    { "AF",	"" },
    /* AB: end a shifted key */
    { "AB",	"" },
    /* vi: make the cursor visible */
    { "vi",	"\033[?25h" },
    /* ve: make the cursor invisible */
    { "ve",	"\033[?25l" },
    { NULL,	NULL }
};

/*
 * The size of the console, if it has one.  Returns 0 if the size
 * cannot be determined.
 */
static void
zsh_win32_console_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    *rows = 0;
    *cols = 0;
    if (h == INVALID_HANDLE_VALUE)
	return;
    if (!GetConsoleScreenBufferInfo(h, &info))
	return;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    *cols = info.srWindow.Right - info.srWindow.Left + 1;
    if (*rows <= 0)
	*rows = 0;
    if (*cols <= 0)
	*cols = 0;
}

/**/
int
tgetent(char *bp, char *name)
{
    /*
     * Every terminal is treated the same way: the capabilities above
     * are understood by the console of Windows 10 and later.  The
     * buffer is not used, the capabilities are static.
     */
    (void) bp;
    (void) name;
    return 1;			/* TGETENT_SUCCESS */
}

/**/
char *
tgetstr(char *id, char **area)
{
    const struct zsh_cap *c;

    (void) area;
    if (!id)
	return NULL;
    for (c = zsh_caps; c->name; c++)
	if (!strcmp(c->name, id))
	    return (char *) c->value;
    return NULL;
}

/**/
int
tgetnum(char *id)
{
    int rows, cols;

    if (!id)
	return -1;
    if (!strcmp(id, "li")) {
	zsh_win32_console_size(&rows, &cols);
	return rows ? rows : 24;
    }
    if (!strcmp(id, "co")) {
	zsh_win32_console_size(&rows, &cols);
	return cols ? cols : 80;
    }
    if (!strcmp(id, "Co"))
	return 256;
    if (!strcmp(id, "sg"))
	return 0;
    return -1;
}

/**/
int
tgetflag(char *id)
{
    if (!id)
	return 0;
    if (!strcmp(id, "am"))
	/* The terminal wraps at the right margin. */
	return 1;
    if (!strcmp(id, "xn"))
	/* The terminal does not have the newline wraparound glitch. */
	return 0;
    return 0;
}

/**/
int
tputs(char *cp, int affcnt, int (*outc)(int))
{
    if (!cp)
	return 0;
    while (*cp)
	outc((unsigned char) *cp++);
    (void) affcnt;
    return 0;
}

/*
 * Move the cursor using a parameterised capability.  The escape strings
 * above use the termcap "%p1%d" notation, so expand that here.  By
 * termcap convention the first parameter is the row and the second is
 * the column, even though tgoto() itself takes the column first.
 */
char *
tgoto(const char *cap, int col, int row)
{
    static char buf[64];
    char *out = buf, *end = buf + sizeof(buf) - 1;
    int params[3];
    int stack[8];
    int sp = 0;

    if (!cap)
	cap = "";
    params[1] = row;
    params[2] = col;

    while (*cap && out < end) {
	if (*cap != '%') {
	    *out++ = *cap++;
	    continue;
	}
	cap++;
	if (*cap == '%') {
	    *out++ = '%';
	    cap++;
	    continue;
	}
	if (*cap == 'i') {
	    params[1]++;
	    params[2]++;
	    cap++;
	    continue;
	}
	if (*cap == 'p' && (cap[1] == '1' || cap[1] == '2')) {
	    if (sp < (int) (sizeof(stack) / sizeof(stack[0])))
		stack[sp++] = params[cap[1] - '0'];
	    cap += 2;
	    continue;
	}
	if (*cap == '+' || *cap == '-') {
	    int sign = (*cap == '+') ? 1 : -1;
	    int n = 0;
	    cap++;
	    while (*cap >= '0' && *cap <= '9')
		n = n * 10 + (*cap++ - '0');
	    if (sp)
		stack[sp - 1] += sign * n;
	    continue;
	}
	if (*cap == 'd' || *cap == 'c') {
	    int val = sp ? stack[--sp] : 0;
	    if (*cap == 'c') {
		*out++ = (char) val;
	    } else {
		char tmp[16];
		int len = snprintf(tmp, sizeof(tmp), "%d", val);
		if (len < 0)
		    len = 0;
		if ((size_t) len >= sizeof(tmp))
		    len = (int) sizeof(tmp) - 1;
		if (out + len > end)
		    len = (int) (end - out);
		memcpy(out, tmp, len);
		out += len;
	    }
	    cap++;
	    continue;
	}
	/* Unknown directive: copy it through unchanged. */
	*out++ = '%';
	if (*cap && out < end)
	    *out++ = *cap++;
    }
    *out = '\0';
    return buf;
}
