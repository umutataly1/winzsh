# zsh for Windows (native Win32 port)

This directory contains everything needed to build zsh as a **native
Windows x64 executable** (`zsh.exe`).  There is no POSIX emulation layer
involved: no Cygwin, no MSYS, no WSL.  The shell talks to the Win32 API and
the UCRT directly, and the few Unix interfaces zsh expects (`fork`,
`select`, `termios`, `kill`, `getrlimit`, …) are implemented in this
directory on top of Win32 primitives.

## Building

Any mingw-w64 style cross compiler works; the port was developed with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (UCRT).  In this
workspace it is installed in `~/.local/bin/llvm-mingw`, with symlinks
for the `x86_64-w64-mingw32-*` tools in `~/.local/bin` (which is on
`PATH`), so the `PATH` line below is only needed for a toolchain kept
somewhere else.

```sh
export PATH=/path/to/llvm-mingw/bin:$PATH
cd /path/to/zsh
make -f Src/win32/Makefile BUILDDIR=$PWD/build
```

The result is `build/zsh.exe`.  autoconf is not used: `Src/win32/gen.py`
produces the files that a normal build gets from `configure`
(`config.h`, `signames.h`, `zshpaths.h`, the module list, …), and
`Src/win32/config.h` is the hand-written configuration for Win32.

The installation prefix compiled into the binary defaults to
`C:\Program Files\zsh`; override it at build time:

```sh
ZSH_WIN32_PREFIX='C:\zsh' make -f Src/win32/Makefile BUILDDIR=$PWD/build
```

`make -f Src/win32/Makefile install` stages a tree that mirrors that
prefix (under `build/install` by default):

```
install/bin/zsh.exe
install/share/zsh/functions/...          # autoloadable functions
install/share/zsh/functions/Completion/  # completion system (compinit)
install/share/zsh/site-functions/        # drop-in directory
```

Copy the contents of the staging directory to the prefix on the Windows
machine.

## Running the test suite under Wine

```sh
wine build/zsh.exe -f -c 'print -r -- $ZSH_VERSION'
```

Interactive use needs a real console.  Under Wine that means a pty; see
`/tmp/opencode/t/ptyrun2.py` in the development environment for a small
harness that feeds keystrokes to a pty and dumps the raw output.

## Design notes

### Processes and `fork`

Windows has no `fork`.  `zsh_win32_fork()` in `zshwin32.c` starts a fresh
`zsh.exe --zsh-win32-subshell <state-file> <cmd-file>` child, where the
state file is a dump of the parent's shell state (options, parameters,
aliases, functions, traps and loaded modules) written with `typeset -p`
syntax, and the command file holds the serialised command text.  The child
applies the dump and runs the command, so command substitution, pipelines,
subshells, `(...)`, background jobs and process substitution all behave as
in a forked shell.

`zsh_win32_dump_modules()` writes `zmodload` lines, so modules that were
loaded in the parent are loaded in the child as well (this is why
`$(strftime …)` or `$(( sqrt(9) ))` work).

### Running external commands

`zexecve()` uses `zsh_win32_execve()` rather than the C runtime's
`execve()`.  The runtime function builds the command line without any
quoting, so an argument that contains a space — for example the shell's
own path in `C:/Program Files/zsh/bin/zsh.exe` — arrives at the child
split into several arguments.  `zsh_win32_execve()` quotes the arguments
itself, but only those that need it: `cmd.exe` parses its own command
line and is confused by quotes around simple words.  The environment is
passed as a block built from `environ`, and `argv[0]` is rewritten to use
backslashes when it looks like a path, because `cmd.exe` does not expect
forward slashes there.

Windows cannot replace the running process image, so the program is
started as a child and the shell process waits for it and then exits with
the same status (including the `0x10000 | signo` encoding above).  To the
rest of the shell this is invisible: `execve()` never returns on success.

A command word that contains a directory separator is treated as a path
rather than looked up in `PATH`; the test uses `IS_DIRSEP()`, so both
`C:/Windows/System32/cmd.exe` and `C:\Windows\System32\cmd.exe` work.

### Signals

`signal.c` implements a small signal system:

* `SIGINT` comes from `SetConsoleCtrlHandler`.
* `SIGCHLD` is raised when a child is reaped.
* Signals sent to a process (`kill`) are delivered through a named shared
  memory block plus a named event (`Local\zsh-win32-sig-<pid>`), created by
  the parent when the child is registered, so that there is no race between
  `fork` and `kill`.
* A process that dies from a signal exits with the code `0x10000 | signo`;
  `zsh_win32_child_status()` turns that back into the usual wait status
  (`0200 | signo`), so `$?` is 128+signo as on Unix.

Pending signals are polled at safe points via
`ZSH_WIN32_POLL_SIGNALS()`.

### Console

`term.c` turns the Windows console into something resembling a terminal:

* virtual terminal processing is enabled for output; input and output code
  pages are set to UTF-8 (and restored on exit),
* standard handles are put into binary mode unless they are consoles,
* console input is read and buffered by the shell itself
  (`ReadConsoleW` in cooked mode, `ReadConsoleInputW` with a key
  translation table in raw mode), which lets `select()` report accurately
  whether a character is already waiting — the line editor depends on that
  for escape sequences, and the C runtime's own console buffer would hide
  the truth,
* in raw mode a bare Alt key press is turned into ESC (Wine turns a Unix
  terminal's ESC into an Alt key press), and VK codes are mapped to the
  usual VT sequences for arrows, Home/End, Ins/Del, PgUp/PgDn and F1–F12,
* Windows splits the console into an input handle and an output handle,
  while zsh expects one bidirectional terminal descriptor.  `SHTTY` is
  the input side (that is what the line editor reads and `tcsetattr()`
  configures), so writes to it — the prompt stream `shout` and the
  `write_loop()` calls used by the line editor — are sent to the console
  output handle instead.  `termcap.c` supplies the capabilities zsh asks
  for, and its `tgoto()` expands the `%p1%d` parameters so that cursor
  motion uses the intended escape sequences.

#### The console of Wine does not understand escape sequences

The console of Windows interprets the escape sequences of the line editor
and keeps its own cursor position in step with them.  The console of
Wine does not: it passes the sequences through to the terminal (so the
terminal draws them correctly) but counts *every byte* of them as a
column.  Its idea of the cursor position therefore runs ahead of the
real one, and when the count reaches the width of the console buffer,
Wine wraps the line itself and pushes a newline into the output stream.
That newline lands in the middle of the line being edited and the display
falls apart — for example "print ok" turns into "print o" and "k" on the
next line.

`zsh_win32_widen_console_buffer()` avoids this: under Wine (detected
with `wine_get_version()` in ntdll.dll) the console buffer is made much
wider than the visible window, so the miscount needs thousands of bytes
before it can reach the margin.  The count starts again at every prompt,
because the shell begins a new line with a carriage return, and the
buffer is only widened where the console is used at all — on Windows
itself nothing is changed.

### Files and paths

* `open()` for reading is done in text mode (CRLF → LF), for writing in
  binary mode; a pipe or file used for command substitution therefore
  does not leak `\r` into the result.
* Paths handed to the shell are in the form the rest of zsh expects:
  `getcwd()`, `realpath()` and the compiled-in installation prefix all
  use forward slashes (`C:/Users/me`), and the directory of `$HOME` is
  reported the same way.  Backslashes are still accepted on input (the
  Windows API takes either), but the shell's own path manipulation (the
  `:A`, `:P`, `:h` and `:t` modifiers, the directory cache, `fpath`
  matching) only understands slashes.
* An absolute path is not just one that starts with a slash: a drive
  letter or a UNC name is absolute too.  `ZSH_ISABSPATH()` (in
  `zshwin32.h`, implemented by `zsh_win32_isabs()`) is used where the
  core code used to test `*s == '/'`; without it, a `fpath` entry such
  as `C:/Program Files/zsh/share/zsh/functions` is mistaken for a
  relative path.
* `PATH_SEP` is `;` and `IS_DIRSEP()` accepts `/` and `\`.
* Command lookup follows Windows: an extensionless name is completed
  from `PATHEXT`, so `cmd` runs `cmd.exe`.  This applies both when the
  command is executed and to `whence`, `where` and `hash`.
* `stat()` and `fstat()` replace the mode bits the C runtime reports
  (0777 for every directory, 0666 for every file) with plausible POSIX
  bits: 0755 for directories, 0644 for files, 0555/0444 for objects with
  the read-only attribute, plus the execute bits for `.exe`, `.com`,
  `.bat`, `.cmd` and `.ps1` files.  Windows permissions are ACLs and
  cannot be expressed this way, but scripts (and `compaudit`) do look at
  the bits.
* `getrlimit()` reports `ZSH_MAXFD` for `RLIMIT_NOFILE`; `setrlimit()` is a
  no-op, so `ulimit` is accepted but has no effect.

### Locale and Unicode

`setlocale(LC_ALL, "")` is called at startup, and `nl_langinfo(CODESET)`
reports UTF-8, so `\u`/`\U` escapes and UTF-8 I/O behave like the Unix
build.  Note that Windows locale *names* differ from POSIX ones
(`German_Germany.1252` rather than `de_DE.UTF-8`); the UCRT ignores names
it does not recognise and stays in the `C` locale.

## Known limitations

* **No job control.**  `MONITOR` is off: there are no process groups, so
  `fg`, `bg`, `^Z` and `%job` suspension are not available.  Background
  jobs and `wait` work.
* **No dynamic modules.**  Everything is linked statically; the module
  list is in `Src/win32/modules.txt`.  `zmodload` works for the built-in
  modules.
* **No external utilities are bundled.**  This is the shell, not a
  coreutils replacement; on a stock Windows box `ls`, `mv`, `cat` etc. are
  not available unless something provides them.  zsh's own file builtins
  (`zmodload zsh/files` → `zf_mv`, `zf_rm`, …) can be used instead.  One
  visible consequence: `compinit` needs `mv` to install its dump file, and
  without it every shell start rescans the completion functions (~15 s on
  Wine).  Either install a `mv` from somewhere, or put

  ```zsh
  zmodload zsh/files
  alias mv=zf_mv
  ```

  in your `.zshrc` before `compinit`.  The `compaudit` security check that
  `compinit` performs needs the emulated mode bits described above and no
  longer needs `compinit -u`.
* **No `MULTIBYTE_SUPPORT`.**  String handling is byte based (the same as
  a Unix zsh built without multibyte support); UTF-8 output works.
* **`fork` is emulated.**  A child is a new `zsh.exe` that restores a state
  dump, so process creation is slower than on Unix, and things that cannot
  be expressed in a dump (open file descriptors other than 0/1/2,
  uncatchable compiler state) do not survive a `fork`.
