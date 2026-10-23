UNIXREADME.txt - zsh for Windows: a guide for Unix people
==========================================================

You already know zsh.  This file is not about zsh.  It is about the
surface you are standing on when you run zsh on Windows, and where that
surface differs from the Unix you are used to.  Read it once and you
will save yourself an afternoon of confused debugging.

This is written for people who know Unix well.  It does not explain what
a shell is, what $PATH means, or how pipes work.  It assumes you know
all of that and tells you what changed.

Installation is covered in INSTALLATION.txt and is not repeated here.
Assume zsh.exe is on your PATH and busybox is installed next to it; the
rest of this file is about what happens after that.


1. The mental model shift
-------------------------

On Unix, the shell is a program that talks to a kernel through system
calls.  The kernel provides processes, signals, file descriptors,
terminals, a filesystem, and so on.  zsh on Unix is a consumer of those
services.

On Windows, none of those services exist in Unix form.  Windows has its
own kernel (the NT kernel), its own process model, its own I/O model,
its own filesystem, its own permission model, and its own terminal
model.  zsh.exe is a consumer of *those* services, translated through
the Win32 API and through a small compatibility layer compiled into the
binary.

The practical consequence: zsh on Windows behaves like zsh, but the
*world it runs in* is Windows.  Files are where Windows puts them.
Processes are Windows processes.  Permissions are ACLs.  Paths have
drive letters.  There is no /etc, no /usr, no /dev, no /proc, no fork,
no process groups.

Keep that model in mind -- "the shell is the same, the world underneath
is Windows" -- and everything else follows.

Two useful sanity checks at any time:

  print -r -- $OSTYPE      # windows
  print -r -- $ZSH_VERSION # the zsh release this is based on


2. Paths
--------

  * Drive letters are the top of the tree.  C:/ is the root of the C
    drive, D:/ the root of D, and so on.  There is no single root
    directory.  "cd /" goes to the root of the *current* drive (C:/ by
    default).

  * Forward slashes work everywhere and are what zsh prints back.
    Backslashes are accepted by Windows, but zsh processes them before
    Windows ever sees them.  Unquoted, "cd C:\Users\me" loses the
    backslashes (they quote the next character) and becomes
    C:Usersme.  Inside single or double quotes they survive:
    cd 'C:\Users\me'.  Forward slashes avoid the whole question.

  * UNC paths work: //server/share/path is the equivalent of
    \\server\share\path.  Note that both forms start with two
    separators, which also makes them absolute.

  * Drive-relative paths (C:foo) behave as in cmd, which is to say
    confusingly: C:foo means "foo relative to the current directory of
    the C drive", not "foo at the root of C".  Avoid them; use C:/foo.

  * $PWD is reported with forward slashes and in the on-disk case, so
    it need not be a byte-for-byte copy of what you typed.

  * ~ expands to your profile directory, taken from USERPROFILE
    (HOMEDRIVE + HOMEPATH if that is unset).  Typically
    C:/Users/<name>.  There is no /home; $HOME points straight at the
    profile directory.

  * Case: the filesystem is case-insensitive by default but
    case-preserving.  "cd c:/users" works, and zsh reports the
    on-disk spelling.  Do not rely on two names differing only in
    case -- you cannot create both.

  * There is no MSYS/Cygwin-style drive prefix.  "/c/Users" is not
    C:/Users; it means "<current drive>:/c/Users" and will not exist.
    Write "C:/Users".  This trips up everyone arriving from Git Bash.

  * Path length: the classic MAX_PATH limit of 260 characters still
    applies unless long-path support is enabled in the registry.
    Deep node_modules trees and nested project directories hit this.
    Keep project roots short, e.g. C:/src.

  * ".." and "." work normally.  "cd D:" switches to drive D, landing
    at its root; it is safer to write "cd D:/" and say what you mean.


3. The filesystem
-----------------

  * There is no /etc, /usr, /bin or /lib in any Unix sense, and /proc
    and /sys do not exist at all.  /dev holds only the handful of
    mappings listed below.  Paths starting with a slash refer to the
    root of the current drive, so "/tmp" means "C:/tmp" (which
    probably does not exist).

  * /dev is small but not empty.  The port translates what zsh and
    portable scripts actually use:
      /dev/null      -> NUL
      /dev/tty       -> CONIN$ when read, CONOUT$ when written
      /dev/stdin     -> a duplicate of fd 0
      /dev/stdout    -> a duplicate of fd 1
      /dev/stderr    -> a duplicate of fd 2
      /dev/fd/N      -> a duplicate of fd N (process substitution)
      /dev/urandom,
      /dev/random    -> the raw disk device; usually fails without
                        administrator rights, so do not use it as an
                        entropy source
    /dev/tty and the CON* names only work when the process actually
    owns a console; redirected or detached, they fail with EBADF.
    Everything else under /dev does not exist.

  * There is no sticky bit, no setuid, no setgid, and no executable
    bit.  "chmod" itself is an external utility you have to install
    (or use zf_chmod from zmodload zsh/files, which takes octal
    modes).  The mode bits are remembered by the CRT because the
    read-only attribute maps onto them; execute permission is decided
    by the extension (.exe, .com, .bat, .cmd, .ps1), not by the mode.
    A shell script with 0755 still will not run by name.

  * Permissions are ACLs.  "ls -l" shows a synthesized rwx string
    (0755 for directories, 0644 for files, execute bits added for the
    extensions above), not the real security descriptor.  To inspect
    or change the real thing use icacls or PowerShell's Get-Acl /
    Set-Acl.  "compaudit" works because it only looks at the
    synthesized bits.

  * A shell script named "foo" with a shebang will not run by typing
    "foo": there is no execve of scripts.  Run it as "zsh foo", or
    write a .cmd wrapper, or install it as a function.

  * Symlinks: Windows supports them, but creating one needs Developer
    Mode or SeCreateSymbolicLinkPrivilege.  "ln -s" may fail with
    "operation not permitted" on a stock account.  Junctions are the
    unprivileged alternative for directories.

  * Hidden files are not dotfiles.  The hidden attribute is metadata,
    not a naming convention.  Windows also marks desktop.ini,
    Thumbs.db, $RECYCLE.BIN and friends hidden, so "ls -a" is not the
    same as "show everything Windows hides".

  * File names cannot contain any of: < > : " / \ | ? * and cannot end
    in a dot or a space.  Reserved device names (CON, PRN, AUX, NUL,
    COM1..COM9, LPT1..LPT9) cannot be used as ordinary file names in
    any directory.  A file called "con.txt" is not a text file.

  * Deleting an open file fails.  Unix lets you unlink a file that is
    still open; Windows does not (unless it was opened with delete
    sharing).  Scripts that write a file, keep a handle open, and
    unlink it will fail.  Close first, then delete.

  * Hard links work on NTFS but not on FAT32 or exFAT.  There is no
    "ln" unless you install one; zf_ln from zmodload zsh/files makes
    hard links, and "ln -s" (busybox or coreutils) needs the symlink
    privilege described above.

  * "$RECYCLE.BIN" and "System Volume Information" appear at the root
    of every volume.  Access to the latter is denied; ignore it.


4. Environment variables
------------------------

  * Names are case-insensitive.  $PATH and $Path are the same
    variable.  zsh keeps a single map, so exporting both spellings
    does not create two variables.

  * Values use ";" as the separator for PATH, PATHEXT, and friends,
    not ":".  Do not assume ":" separates path lists anywhere except
    inside zsh's own $path array.

  * zsh's $path array is tied to $PATH and splits on the platform
    separator, so "typeset -U path" and "path+=/new/dir" work
    normally.  A PATH entry that contains a colon (rare but legal in
    a directory name) will confuse the array; Windows has the same
    problem with ";" in a name.

  * "export" makes a variable visible to child processes exactly as on
    Unix.  There is no "env" command built in; busybox provides one.

  * Variables the OS gives you, worth knowing:
      USERPROFILE      your home directory, e.g. C:\Users\me
      HOMEDRIVE        drive letter of your home, e.g. C:
      HOMEPATH         path part of your home, e.g. \Users\me
      SYSTEMROOT       C:\Windows
      TEMP, TMP        per-user temp directory
      PATHEXT          executable extensions
      COMSPEC          path to cmd.exe
      NUMBER_OF_PROCESSORS

  * The port synthesizes the Unix names:
      HOME      <- USERPROFILE (or HOMEDRIVE+HOMEPATH)
      USER,
      LOGNAME   <- USERNAME
      SHELL     = /usr/bin/zsh (a fiction, but scripts look at it)
      TERM      = xterm-256color
      OSTYPE    = windows

    LANG is *not* synthesized: it is whatever the environment already
    has, which on a stock Windows box is nothing.  The port calls
    setlocale(LC_ALL, "") and reports UTF-8 as the codeset either way,
    so an unset LANG does not break UTF-8 output.

    TMPDIR is not set.  Use $TEMP (which the OS supplies) or set
    TMPDIR yourself if a script insists on it.

  * Windows has two environments, user and system, edited in the
    Environment Variables dialog or with "setx".  Changes affect new
    processes only; an already-running zsh keeps the environment it
    started with.


5. Processes and fork
---------------------

  * There is no fork().  Windows creates processes with CreateProcess,
    which needs an executable image.  The port emulates fork by
    spawning a fresh zsh.exe with "--zsh-win32-subshell", dumping the
    parent's options, parameters, aliases, functions, traps and loaded
    modules into a state file, and replaying that into the child.  So
    command substitution, pipelines, subshells, background jobs and
    process substitution all behave as on Unix -- but a child costs
    much more than a fork.

  * Practical consequence: $(...) in a hot loop is slow.  Prefer
    parameter expansion (${var#pat}, ${(f)var}), zsh builtins, and
    reading files with $(<file) rather than $(cat file).

  * File descriptors other than 0, 1 and 2 do not survive the emulated
    fork.  A construct that opens fd 3 in the parent and expects the
    child to inherit it will not work.

  * There is no process group and no session leader.  See section 6.

  * PIDs are Windows PIDs.  $$ is the PID of the current zsh.exe.

  * Orphans are not reparented to init.  When a parent dies its
    children keep running; there is no init to reap them.  Daemons
    are therefore not cleaned up for you.

  * "ps" is not present.  Use tasklist, or busybox ps (a subset).
    "kill" only works on processes that are children of the current
    shell (see section 6); for everything else use
    taskkill /PID n /F.

  * "getrlimit(RLIMIT_NOFILE)" reports a fixed descriptor limit and
    "setrlimit" is a no-op, so "ulimit -n" is accepted but changes
    nothing.  There are no other resource limits.

  * Windows has no "nice".  Process priority is set at launch, with
    "start /low prog" from cmd, or afterwards with PowerShell's
    (Get-Process prog).PriorityClass; there is no nice value in the
    shell and no way to renice.


6. Signals
----------

This is the section that bites people hardest, so read it twice.

Windows has no POSIX signals.  It has a few console control events
(Ctrl-C, Ctrl-Break, close, logoff, shutdown) and TerminateProcess,
which is an immediate kill with no cleanup and no chance to handle it.

What the port provides:

  * SIGINT works.  Ctrl-C in the console becomes SIGINT; the console
    control handler raises it and zsh's handlers run.  Ctrl-Break is
    also mapped to SIGINT.

  * SIGCHLD works.  It is raised when the shell notices that a child
    has exited, so "trap ... CHLD" fires and the shell's own job
    bookkeeping works.

  * SIGALRM works: alarm() is backed by a real timer thread, and the
    sched module is usable.  Timeouts and periodic work are possible,
    which is not true of most Unix-on-Windows ports.

  * SIGHUP is raised on CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT and
    CTRL_SHUTDOWN_EVENT.  Windows gives the process only a few seconds
    before killing it, so SIGHUP cleanup must be fast and must not
    wait for anything.

  * SIGTERM can be *sent* with "kill" to a child zsh.exe, and the
    child's handlers run.  Nothing translates it to a console event
    for arbitrary programs, so a non-zsh child will not see SIGTERM
    at all; it has to be killed.

  * SIGKILL: "kill -9" calls TerminateProcess on a child.  It is
    immediate and unhandleable, as expected.

  * SIGTSTP/SIGCONT (Ctrl-Z, bg, fg): not available.  Job control is
    compiled out (MONITOR is off) because Windows has no process
    groups.  "&" and "wait" work; suspension does not.  There is no
    "fg" to bring a job back.

  * SIGUSR1, SIGUSR2, SIGWINCH, SIGQUIT: the names exist so that
    scripts compile and "trap" does not error, but none of them is
    ever raised.  In particular there is no SIGWINCH: a resized
    window is noticed when the line editor next starts a line, not
    while a program is running.  Full-screen programs that rely on
    SIGWINCH to redraw (less, vim, top) may not notice a resize until
    you press a key.

  * SIGPIPE is never raised.  A write to a closed pipe fails with
    EPIPE and the shell reports or ignores the error according to its
    own logic; the writer is not killed by a signal the way it is on
    Unix.  Pipelines still terminate because the failing write is
    noticed, but "trap ... PIPE" never fires.

  * "kill" only finds processes that are children of the current
    shell, because the port tracks children in its own table.
    "kill <some other pid>" fails with ESRCH.  "kill -0" is an
    existence check for those children only.

Rules of thumb:

  * Do not design shutdown paths around signals.  Use a flag file, a
    socket, or an explicit shutdown command.
  * Do not use "kill -TERM" on non-zsh programs and assume anything
    happened.
  * Do not rely on SIGWINCH.
  * "kill -9" and "taskkill /F" are the same thing here: immediate
    and unhandleable.


7. Terminals and consoles
-------------------------

  * A Windows console is not a tty.  There are no Unix termios settings;
    there is SetConsoleMode and its flags (ENABLE_LINE_INPUT,
    ENABLE_ECHO_INPUT, ENABLE_PROCESSED_INPUT,
    ENABLE_VIRTUAL_TERMINAL_INPUT/PROCESSING, ...).  The port maps the
    termios API onto those flags as faithfully as it can.

  * isatty(1) is true when stdout is a console, false when it is
    redirected or piped.  Scripts that check it keep working.

  * $TERM defaults to xterm-256color.  If the environment does not
    supply one, the port sets it at startup, because zsh needs a
    terminal description: with an empty $TERM the line editor falls
    back to single-line mode, where the cursor keys are unbound and
    redrawing an edited line corrupts the display.  Set $TERM yourself
    if you need a different value.  zsh itself does not consult a
    terminfo database -- it uses a small hand-written capability table
    compiled into the binary -- but child programs that draw on the
    screen do, and they expect TERM to be set.  Windows Terminal
    understands xterm sequences, so this is the honest value, and it
    is what less, vim and git expect.  Installing a Linux terminfo
    database helps the children.

  * Colours: ANSI SGR sequences work in Windows Terminal and in conhost
    on Windows 10 1511 and later, because the port enables virtual
    terminal processing on the output handle.  A program that writes
    raw escape sequences to a redirected file still writes escape
    sequences into the file -- isatty is the thing to check.

  * ioctl(TIOCGWINSZ) is implemented on top of the console, so zsh
    keeps $COLUMNS and $LINES up to date, and programs that ask the
    terminal its size get a sensible answer.  What is missing is
    SIGWINCH: a resize is noticed when the next prompt is drawn (or
    when the program asks again), not silently while it runs.  A
    full-screen program that only redraws on SIGWINCH may not notice
    a resize until you press a key.

  * "stty" and "tput" are not available -- they are Unix utilities,
    not shell builtins, and this port bundles no userland.  Use
    $COLUMNS/$LINES, and bindkey for key mapping.

  * The line editor handles the keyboard itself: ReadConsoleInputW in
    raw mode, with a table mapping virtual-key codes to the usual VT
    sequences for arrows, Home/End, Insert/Delete, PgUp/PgDn and
    F1-F12.  The console's own virtual terminal *input* mode is
    deliberately disabled so that the two translators do not fight.

  * Ctrl-C, Ctrl-D, Ctrl-U, Ctrl-W, Ctrl-R, Ctrl-L work as on Unix.
    Ctrl-D on an empty line exits the shell.  Ctrl-Z does nothing
    (no job control).  Home, End and Delete are not advertised by the
    built-in termcap, so zsh leaves those sequences unbound; add the
    usual three bindkey lines to .zshrc if you want them.

  * Bracketed paste is enabled (zle emits DECSET 2004).  In Windows
    Terminal, which understands it, a multi-line paste is inserted
    into the editing buffer instead of being executed line by line.
    On a console that does not implement 2004 the markers are ignored
    and pasted newlines act as Enter, so a multi-line paste runs each
    line as it arrives.  If that bites you, paste one command at a
    time, or wrap the text in a quoted here-document.

  * /dev/tty is mapped: reading opens CONIN$, writing opens CONOUT$.
    That is how zsh itself reaches the console.  If the process has no
    console (detached, or a service), the open fails.

  * Mouse reporting is not available: the console does not send xterm
    mouse sequences, so programs that expect them will not work.

  * Unicode in the console: the port asks for code page 65001 (UTF-8)
    and uses the wide-character APIs for the console, so UTF-8 output
    and UTF-8 filenames work in Windows Terminal.  Editing is another
    matter: the line editor counts bytes (see above), so a wide
    character is several "characters" to it.  In an old conhost with a
    raster font, characters outside the font do not render at all.


8. Text, encoding and line endings
----------------------------------

  * The port is built without MULTIBYTE_SUPPORT, exactly like a Unix
    zsh built with --disable-multibyte.  String handling is byte
    based: ${#var} counts bytes, ${var[1,3]} slices bytes, and the
    line editor's "character" operations move one byte at a time.
    Command output in UTF-8 is passed through untouched and is
    correct.  Interactive input is the weak spot: a multi-byte
    character is several separate bytes to the editor, so backspace
    over a CJK character or an emoji deletes one byte and leaves an
    invalid sequence behind.  Put non-ASCII text in a script or a
    file and read it from there when it matters.  This byte-based
    editing is the single largest functional gap compared to a normal
    Linux zsh.

  * The console is switched to UTF-8 while zsh runs, and the wide
    APIs are used for input and output, so text does not go through
    the ANSI code page.  A child program that calls chcp and leaves
    the console on 437 or 1252 can produce mojibake for the rest of
    the session; the port sets the code page at startup, not at every
    prompt.  "cmd /c chcp 65001" fixes it without restarting.

  * Locale names are mapped onto the UCRT's closest match.  A name the
    UCRT does not know leaves you in the C locale, silently, and
    Windows locale names are different from POSIX ones
    (German_Germany.1252 rather than de_DE.UTF-8).  Collation and
    message catalogues will not match a Linux box, and there is no
    locale database to fall back on.

  * Line endings, precisely:
      - Files opened by the shell for reading (source, ., $(<file),
        read < file, redirections) are read in text mode: CRLF is
        converted to LF.  A script saved with Windows line endings
        therefore runs correctly.
      - Pipes, and output captured from external programs
        ($(cmd /c something)), are binary: a CR survives.  printf
        'x\r\n' | read line gives a line ending in CR.  Strip it
        with ${line%$'\r'} or tr -d '\r'.
      - Files written by the shell are written in binary mode, so
        what you print is what lands on disk, with no CR added.
      - .bat and .cmd files must keep CRLF; batch's parser is
        unhappy with bare LF.

  * Filenames are handled through the wide APIs, so Unicode filenames
    work even though zsh's internal strings are bytes.  Invalid UTF-8
    in a filename is the one case that can lose information.


9. Scripting differences
------------------------

  * Shebangs are not used.  On Unix the kernel reads "#!" and starts
    the interpreter; Windows has no such mechanism for text files, and
    zsh treats a "#!" line as an ordinary comment.  "#!/bin/zsh" is
    harmless fiction (there is no /bin/zsh here).  What matters is
    that you start the script yourself: "zsh script" (or a .cmd
    wrapper, or a function).

  * "exec" is emulated: the port starts the program as a child and
    exits with its status.  That is invisible to the rest of the
    shell.  "exec ./script" still fails, because scripts are not
    executable images.

  * Command lookup follows PATHEXT: "python" finds python.exe,
    python.cmd, python.bat, and so on, in PATHEXT order.  This is
    usually convenient, but a stray .bat earlier on PATH can shadow
    the .exe you meant.  "whence -p python" shows what actually runs.

  * The current directory is not searched implicitly.  Use ./prog.

  * cmd.exe builtins (dir, copy, del, cls, type, move, ren, md, rd,
    echo, set) are not programs and cannot be run directly.  Use the
    zsh equivalent (ls is not there either unless busybox provides it;
    use print -l *, cp/mv via zmodload zsh/files, or "cmd /c ...").

  * zsh's own modules work when they are linked in: zsh/files (zf_mv,
    zf_rm, zf_mkdir, zf_ln, zf_chmod), zsh/stat, zsh/datetime,
    zsh/system, zsh/parameter, zsh/sched, zsh/mathfunc, zsh/random,
    zsh/zselect, zsh/zutil, zsh/rlimits, zsh/watch, zsh/nearcolor.
    Others -- notably zsh/net/tcp and zsh/zpty -- are not compiled in,
    and neither is a terminfo reader.  The full list is in
    Src/win32/modules.txt.  "zmodload zsh/foo" for anything not in
    that list fails, and there is no dynamic module loading.

  * Glob qualifiers, extended globbing, parameter expansion flags,
    arrays, associative arrays, coproc, process substitution, here
    documents and here strings all work.  Process substitution is
    built on an anonymous pipe plus /dev/fd/N rather than a fifo, and
    it pays the cost of the emulated fork, but the syntax behaves as
    on Unix.

  * "print -r --" is your friend when passing Windows paths around:
    it stops zsh from interpreting backslashes as escapes.  Rule of
    thumb: any string that came from Windows (a path, a registry
    value, a command line) should be printed with -r and quoted.

  * Exit statuses are masked to 8 bits, as on Unix.  A Windows
    program returning 300 shows up as 44 in $? (300 & 0xFF).  Programs
    that rely on the full 32-bit status -- some installers and msiexec
    -- will look successful or failed for the wrong reason.  Check
    their own log, not $?.

  * $pipestatus works and holds each stage's status.  Note that the
    masking in the previous paragraph applies to each element.

  * "ulimit" is accepted but has no effect (setrlimit is a no-op).
    Filesystem quotas and memory limits are Windows policy, not shell
    settings.

  * "jobs" lists background jobs and "wait" waits for them.  There is
    no "fg", no "bg" and no Ctrl-Z (see section 6).

  * Redirections to /dev/null and NUL work.  CON, CONOUT$ and CONIN$
    are opened as devices (no create or truncate), but only when the
    process has a console; otherwise the open fails with EBADF.

  * Output buffering is the C runtime's, not the shell's: stdout to a
    console is line-buffered, stdout to a file or pipe is fully
    buffered.  A program that prints without a newline and does not
    flush can look "stuck" or lose the last line when redirected.
    Use the program's own flush or line-buffering option; there is no
    stdbuf unless you build one.

  * Command substitution and pipelines fork a whole zsh.exe with a
    state dump (section 5).  This is correct but several times more
    expensive than on Unix.  If a script is slow, count the $(...).


10. What to install alongside zsh
---------------------------------

zsh gives you a shell, not a userland.  On Linux, coreutils, grep, sed,
awk, find, tar and friends are already there.  On Windows they are not.

The recommended companion is a static busybox for win64:

  * one ~1 MB executable
  * ls, cat, cp, mv, rm, mkdir, grep, sed, awk, find, tar, gzip,
    wget, ps, kill, env, xargs, sort, uniq, head, tail, wc, tr, cut,
    diff, md5sum, sha256sum, and a few hundred more
  * POSIX flags, the same ones you know
  * no installer, no DLLs, no POSIX emulation layer

Put busybox.exe somewhere on PATH and create the applet links you
want, or call "busybox ls" explicitly.  A common pattern is a directory
of hardlinks or small wrapper scripts named after each applet.

One concrete reason to install it: compinit wants "mv" to install its
dump file.  Without any mv, every shell start rescans the completion
functions.  Either provide busybox mv, or put this before compinit:

    zmodload zsh/files
    alias mv=zf_mv

What you should *not* install for this port:

  * Cygwin -- a full POSIX emulation layer with its own DLL and its
    own filesystem view.  It works, but it is a second world, and its
    paths (/cygdrive/c/...) are not the paths zsh sees.
  * MSYS2 -- same idea, aimed at building native Windows software.
    Fine as a build environment, confusing as a daily shell.
  * WSL -- a real Linux kernel in a VM.  Excellent for Linux work, but
    it is Linux, not Windows: it cannot run your Windows zsh.exe, and
    its files live on a virtual disk behind a translation layer.
  * Git Bash -- a cut-down MSYS2 that ships with Git.  It has its own
    bash and its own /usr, and mixing it with zsh produces two
    incompatible path universes.

The point of this port is the opposite of those: one native zsh.exe,
using Windows paths, Windows processes, and Windows programs directly,
with no emulation layer in between.


11. Things that will surprise you
---------------------------------

  * Deleting a directory that is somebody's current directory fails.
  * Renaming an open file usually fails.
  * You cannot delete a running .exe.
  * Two processes cannot both open the same file for writing unless
    sharing was requested.  Unix's "everyone can open it" is not the
    default.
  * "mv" across drives is copy plus delete, not a rename.  It is not
    atomic and can be slow.
  * Timestamps have 100 ns resolution in the API but are often coarse
    in practice, and last-access updates may be disabled entirely.
  * "ls -l" link counts are synthetic and mostly meaningless.
  * Sparse files, reflinks and extended attributes have no Unix
    equivalent; "cp --reflink" is meaningless.
  * "nohup" is not useful.  Use "start /b" from cmd, a Windows
    service, or Task Scheduler.
  * "cron" does not exist.  Use Task Scheduler (schtasks).
  * There is no double-fork daemon idiom.  A process either owns a
    console or is detached at creation time.
  * "sudo" does not exist and cannot be emulated properly.  Elevation
    means launching a new process behind a UAC prompt, not a setuid
    bit.
  * ssh-agent works if you install the OpenSSH optional feature
    (Windows 10 1809 and later).
  * Unix domain sockets exist on Windows 10 1803 and later, but most
    tools use TCP or named pipes instead.
  * Named pipes are the native IPC primitive: \\.\pipe\name.  They
    are not files and do not appear in the filesystem.
  * Process creation is slower than on Unix for a second reason:
    antivirus software (Defender included) inspects every new image.
    Build trees with thousands of short-lived processes feel this.
    Adding an exclusion for your build directory is the usual fix.
  * The console keeps its own scrollback buffer.  There is no
    "clear" command unless you install one (busybox has one); the
    console's own buffer and Windows Terminal's scrollback are
    controlled by the terminal, not by zsh.


12. Quick translation table
---------------------------

  Unix                          Windows / zsh port
  ----                          ------------------
  /                             C:/ (root of current drive)
  /home/me                      C:/Users/me  ($HOME, $USERPROFILE)
  /tmp                          C:/Users/me/AppData/Local/Temp ($TEMP)
  /etc                          C:/ProgramData  (roughly)
  /usr/bin, /bin                directories on $PATH
  /dev/null                     NUL  (the port also maps /dev/null)
  /dev/tty                      CONIN$/CONOUT$ ($TTY is CONIN$)
  /dev/urandom                  raw disk mapping; usually fails
  $PATH split on ":"            $PATH split on ";"
  chmod +x file                 extension in PATHEXT decides
  ln -s target link             mklink (privilege) or a junction
  ps aux                        tasklist  (or busybox ps)
  kill -9 pid                   a child: TerminateProcess; else use
                                taskkill /PID pid /F
  kill -TERM pid                only to a child zsh.exe
  df -h                         powershell -c "Get-PSDrive C"
  mount                         net use  (mapped drives are automatic)
  crontab -e                    schtasks /create
  nohup cmd &                   start /b cmd
  sudo cmd                      elevated console / runas
  apt install pkg               winget install pkg
  systemctl status svc          sc.exe query svc
  /proc/cpuinfo                 powershell -c "Get-CimInstance Win32_Processor"
  uname -a                      cmd /c ver  (or $OSTYPE in zsh)


13. Sanity checklist when something feels wrong
-----------------------------------------------

  1. Is the path a Windows path?  A leading "/" means the current
     drive's root, not the Unix root.
  2. Is the program a real file on disk?  cmd builtins and PowerShell
     cmdlets are not.
  3. Did a CR sneak in from a pipe or an external program?  File
     reads strip it; pipes do not.
  4. Is the file locked by another process?  Windows will not let you
     delete or overwrite it.
  5. Is this a signal that does not exist here?  (SIGWINCH, SIGUSR1,
     SIGUSR2, SIGTSTP)
  6. Is this a fork that has to replay the shell state?  (slower, but
     correct)
  7. Is the terminal VT-capable?  (Windows Terminal yes, old conhost
     no)
  8. Is the exit status truncated?  Windows codes are 32-bit; zsh
     reports the low 8 bits.  300 becomes 44.
  9. Is multi-byte text involved?  The build is byte-based, so
     backspace and ${#var} work in bytes.
 10. Is the console's input mode still what zsh left it as?  A child
     program that changes it can make the next prompt misbehave.

If you remember nothing else: the shell is the same, the world is
different.  When something behaves oddly, ask "what does Windows do
here?" before "what did zsh do wrong?"  Almost every surprise in this
file comes from that boundary.

What *is* the same, and worth saying plainly: the .zshrc, the
completion system, the history, the globbing, the parameter expansion,
the prompt escapes, the key bindings, the functions, the scripts --
all of it is zsh, not a reimplementation.  Write your configuration
once and move it between Windows and Linux unchanged, except for the
parts that mention a path.


