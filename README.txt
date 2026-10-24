zsh for Windows - README
=========================

CREDITS TO Z SHELL
------------------
This is forked of from the offical zsh source code, without
zsh and their developers this project woulduntve been possible.
All original zsh copyright notices and applicable license terms are retained.
winzsh is an independent community project porting z shell to windows 10-11
THIS IS NOT AN OFFICAL RELEASE.

What this is
------------

zsh.exe is the Z shell, built as a native 64-bit Windows program.

It does not need WSL, Cygwin, MSYS2, or any POSIX emulation layer.  It
talks to Windows and the Microsoft C runtime directly, and implements
everything zsh expects from a Unix system (processes, signals, the
terminal, file permissions, ...) on top of the Win32 API.

The point of this project is to have a real zsh on Windows: the same
shell syntax, the same completion system, the same prompts, the same
key bindings as on Linux or macOS, but running as a normal Windows
console application that you can drop next to cmd.exe or PowerShell.

If you have used zsh on Linux, almost everything you know carries over.
If you have never used zsh before, this is still a good shell to start
with: it has tab completion, spelling correction, a powerful history,
and a prompt you can customise down to the last character.

Why switch to zsh
-----------------

If you are on Windows, you already have cmd.exe and probably PowerShell.
Here is why you might want zsh instead, or alongside them.

  vs. cmd.exe
    cmd is fine for running programs, but its scripting language is
    archaic: no arrays, no real functions, no local variables, no
    arithmetic beyond basic integer math, no decent string handling,
    and a quoting model that makes complex commands painful.  zsh gives
    you a real programming language (arrays, associative arrays,
    functions with local variables, floating point, regex, glob
    qualifiers) while still letting you run every Windows program by
    name.  Tab completion in zsh is also in a different league: it
    completes commands, options, file names, git branches, and more,
    with descriptions.

  vs. ash / dash / busybox sh
    ash (the Almquist shell, used by busybox and many embedded
    systems) is fast and POSIX-compliant, but it is deliberately
    minimal.  No arrays, no associative arrays, no advanced globbing,
    no spell correction, no programmable completion, no prompt
    customisation beyond the basics.  zsh is what you move to when you
    outgrow ash.  If you only need a shell to run scripts, ash is
    fine; if you want an interactive shell you actually enjoy using,
    zsh is the answer.

  vs. PowerShell
    PowerShell is a genuinely good shell and a real programming
    language, but it is its own thing: Verb-Noun cmdlets, object
    pipelines, .NET types, and a syntax that does not resemble
    anything from the Unix world.  If your work touches Linux, macOS,
    containers, SSH sessions, or CI pipelines, you end up maintaining
    two mental models.  zsh is the same shell everywhere: the same
    .zshrc, the same key bindings, the same completion system, the
    same scripts, on Windows and on Unix.  You also get the entire Unix
    tool ecosystem (via busybox) with the same flags and output format
    you are used to, instead of learning Get-ChildItem and
    Select-String.

  The honest summary
    cmd is for running programs.  PowerShell is for administering
    Windows.  zsh is for people who want a Unix shell that does not
    require a Unix.  You do not have to pick one: many people keep
    PowerShell for Windows administration and zsh for everything else.

What "native" means here
-------------------------

"Native" in this context means the resulting binary is a normal Windows
PE executable.  It links only KERNEL32.dll, bcrypt.dll and the UCRT (the
C runtime that ships with Windows 10 and 11).  There is no cygwin1.dll,
no msys-2.0.dll, no WSL interop, no POSIX emulation layer that has to
be installed or kept on PATH.  You can copy zsh.exe to a bare Windows
machine and run it.

It does NOT mean "zero translation code".  zsh is deeply tied to Unix:
it assumes fork, signals, termios, a terminfo database, Unix file
modes, and so on.  All of that is reimplemented on top of Win32 inside
the binary, in a small compatibility layer (Src/win32/compat.c and
friends) that is compiled into zsh.exe rather than shipped as a
separate DLL.  Every port of a Unix program to Windows does this
internally; the difference between "native" and "emulated" is whether
that translation happens inside your binary (this port) or in a shared
DLL the binary depends on (Cygwin/MSYS).  The recommended busybox win64
port uses the same approach, which is why the two fit together cleanly.

Why this is not WSL, Cygwin or MSYS2
--------------------------------------

There are several ways to run a Unix shell on Windows.  This port is
none of them, and the difference matters:

  WSL (Windows Subsystem for Linux)
    Runs a real Linux kernel (WSL2) or a Linux-compatible syscall
    layer (WSL1) inside Windows.  You get a full Linux distribution,
    a real filesystem, and real Unix semantics.  The cost is a large
    install, a separate kernel or compatibility layer, and a boundary
    between the Linux and Windows worlds: files live in different
    places, paths do not translate automatically, and Windows programs
    are reached through interop rather than being first-class.

  Cygwin
    A POSIX emulation layer implemented as cygwin1.dll.  Programs are
    compiled against the Cygwin API and run against the DLL, which
    translates POSIX calls to Win32 at runtime.  It is mature and
    complete, but every program depends on the DLL, the DLL has to be
    kept on PATH, and programs behave slightly differently from native
    Windows programs (different path handling, different signals,
    different file modes).

  MSYS2
    A fork of Cygwin aimed at building native Windows software.  It
    provides a POSIX shell and a package manager, but the same DLL
    dependency and the same behavioural differences apply.

  This port (zsh.exe)
    A single native Windows executable.  No DLL to install, no kernel
    to boot, no separate filesystem.  It runs like any other Windows
    console program: it starts instantly, it is on PATH like cmd.exe,
    Windows programs are first-class citizens, and files live where
    Windows puts them (C:\Users\<you>).

The trade-off is that this port does less than the others.  It is a
shell, not a Unix environment: there is no ls, no grep, no fork in the
Unix sense, no job control, no /etc.  If you need a full Unix
environment, use WSL.  If you need a Unix toolchain for building
software, use MSYS2.  If you want a fast, native zsh that feels like
zsh everywhere else and plays nicely with Windows, this is it.

The recommended setup is zsh plus the busybox win64 port: zsh gives
you the shell, busybox gives you the tools, and neither needs an
emulation layer.  That combination is as close to "Unix on Windows
without emulation" as you can get today.


Why you might want it
---------------------

  * You want a Unix-style shell on Windows without installing a whole
    distribution (WSL) or a compatibility DLL (Cygwin/MSYS).

  * You want zsh's completion system (compinit), which completes
    commands, options, file names, git branches, ... and is the reason
    many people use zsh in the first place.

  * You want one shell that behaves the same on your Linux box and on
    your Windows box: same .zshrc, same key bindings, same scripts.

  * You want to run zsh scripts on Windows CI or build machines without
    a Unix layer.

  * You want a console application that is a single file (zsh.exe) plus
    a directory of autoloadable functions.  No installer, no service,
    no background process.


What is in the package
----------------------

  bin\zsh.exe                      the shell
  share\zsh\functions\...          autoloadable shell functions
  share\zsh\functions\Completion\  the completion system (compinit)
  share\zsh\site-functions\        empty directory for your own functions

The layout matters: the installation prefix is compiled into zsh.exe.
A binary built with the default prefix expects to find its function
library under

  C:\Program Files\zsh\share\zsh\functions

If you keep the files together in that directory, everything is found
automatically.  See INSTALLATION.txt section 7 if you want a different
location.


Quick start
-----------

  1. Copy the contents of the package (the bin and share directories)
     to C:\Program Files\zsh.

  2. Add C:\Program Files\zsh\bin to your PATH.

  3. Open a new console window and run:

         zsh

  The prompt changes to "hostname% " and you are in zsh.  Type "exit"
  to leave.

You can also start it without changing PATH:

  "C:\Program Files\zsh\bin\zsh.exe"

zsh reads its startup files from your home directory:

  C:\Users\<your name>\.zshrc      interactive shells
  C:\Users\<your name>\.zshenv     always, first
  C:\Users\<your name>\.zprofile   login shells
  C:\Users\<your name>\.zlogin     login shells, after .zshrc
  C:\Users\<your name>\.zlogout    login shells, on exit

$HOME is taken from the HOME environment variable if it is set,
otherwise from USERPROFILE, so normally it is C:/Users/<your name>.
Paths inside the shell are written with forward slashes; backslashes
are accepted when you type them, but "C:/Users/me" is the form zsh
reports back.

A minimal .zshrc:

  # history
  HISTFILE=~/.zsh_history
  HISTSIZE=10000
  SAVEHIST=10000
  setopt HIST_IGNORE_DUPS SHARE_HISTORY

  # prompt
  PS1='%n@%m:%~%# '

  # the editing keys of Windows terminals (zsh does not bind them itself)
  bindkey '^[[H'  beginning-of-line
  bindkey '^[[F'  end-of-line
  bindkey '^[[3~' delete-char

  # completion
  zmodload zsh/files
  alias mv=zf_mv
  autoload -Uz compinit && compinit

If you are used to a full zsh setup, your existing .zshrc will mostly
work; files simply live under C:/Users/<your name> instead of /home.


The one thing that is missing: Unix tools
----------------------------------------

This is the part that bites people first, so it is worth being explicit
about.

zsh is a shell.  It is not a collection of Unix utilities.  Windows
does not ship ls, cat, grep, sed, awk, find, sort, head, tail, or any
of the rest, and this port does not add them.  If you type "ls" you get
"command not found", the same as you would on a bare Linux box with no
packages installed.

Things that work out of the box:

  * cmd /c dir, cmd /c type file.txt, cmd /c copy ...
    Any Windows command is reachable through "cmd /c".

  * zsh's own builtins: print, read, echo, test, [[, for, while,
    if, case, functions, arrays, parameter expansion, globbing,
    redirections, pipelines, job control (background jobs and wait).

  * zsh's file builtins, after "zmodload zsh/files":
    zf_mv, zf_rm, zf_rmdir, zf_mkdir, zf_ln, zf_chmod, zf_chown,
    zf_chgrp, zf_sync.  The INSTALLATION.txt example aliases mv to
    zf_mv so that compinit can save its dump file.

  * Windows programs by name: notepad, cmd, powershell, git, python,
    code, ...  Anything on PATH works, and extensionless names are
    completed through PATHEXT, so "notepad" runs notepad.exe.

So you can get by with cmd /c for the odd listing or copy, and with the
builtins for everything else.  But if you want the real thing, read on.


The fix: install the busybox win64 port
--------------------------------------

The cleanest way to get a Unix toolset on Windows without installing
Cygwin or MSYS2 is the busybox win64 port.  Busybox is a single
executable that contains ls, cat, grep, sed, awk, find, sort, head,
tail, wc, cut, tr, xargs, tar, wget, vi and dozens more, selected by
the name you invoke it under (or by "busybox <applet>").

  1. Download the busybox win64 binary.  The project is hosted at

       https://frippery.org/busybox/

     (look for the "busybox64.exe" / "busybox64u.exe" (UTF-8) build).
     The "u" (UTF-8) variant is recommended so that file names with
     non-ASCII characters are handled correctly.

  2. Put busybox64u.exe somewhere on your PATH, for example
     C:\Program Files\busybox\, and rename it to "busybox.exe" if you
     prefer.

  3. Either invoke applets explicitly:

       busybox ls -la
       busybox cat file.txt
       busybox grep pattern file

     or create small wrapper batch files / shims so that "ls", "cat",
     "grep" ... resolve to "busybox <applet>".  Many people keep a
     directory of shims on PATH:

       C:\Users\<you>\bin\ls.cmd   containing:  @busybox ls %*
       C:\Users\<you>\bin\cat.cmd  containing:  @busybox cat %*

  4. Add the busybox directory (and your shim directory) to PATH.

After that, "ls", "cat", "grep", "find", "sed", "awk", "sort", "head",
"tail", "wc", "cut", "tr", "xargs", "tar", ... all work from inside
zsh, and tab completion completes them like any other command.

Why this is the recommended route:

  * One file, no installer, no service, no background process.  It
    matches the philosophy of this zsh port (a single exe plus a
    directory of functions).

  * No POSIX emulation layer.  busybox win64 is a native Windows
    binary; it does not pull in cygwin1.dll or msys-2.0.dll.  Mixing
    it with this zsh keeps the whole stack native.

  * It is small (a few hundred kilobytes) and fast to start.

  * It covers the 80% of "Unix tools" that shell scripts and
    interactive use actually need.  The remaining 20% (git, python,
    node, ...) you would install anyway, and each of those is a native
    Windows program with its own PATH entry.

  * It does not fight zsh over PATH or over file-name handling: busybox
    applets accept both / and \ as separators and understand Windows
    drive letters, so "busybox ls C:/Users/me" works.

Things to be aware of:

  * busybox applets are deliberately minimal.  "ls" does not have every
    GNU ls option, "grep" is not GNU grep, "sed" is not GNU sed.  For
    day-to-day use this is fine; for scripts that depend on GNU
    extensions you may want the real thing.

  * Some applets behave slightly differently from their Unix
    counterparts because the underlying filesystem is NTFS (case
    insensitive, no symlinks in the Unix sense, ACLs instead of mode
    bits).  This is the same limitation zsh itself has on Windows and
    is documented in INSTALLATION.txt section 10.

  * busybox does not provide a compiler, a debugger, or large
    applications.  For those, install the native Windows ports (MinGW,
    LLVM, Python, Node, Git, ...) and put them on PATH.  zsh finds
    them the same way it finds any Windows program.

In short: zsh gives you the shell, busybox gives you the tools, and
together they give you a usable Unix-like environment on Windows with
no emulation layer at all.  If you only install one extra thing, make
it busybox.


Completion
----------

The completion system is included.  Put this near the end of .zshrc:

  zmodload zsh/files
  alias mv=zf_mv
  autoload -Uz compinit && compinit

The first time, compinit scans the function library, which takes a few
seconds, and writes a dump file (~/.zcompdump).  Later shells read the
dump and start quickly.  If "mv" is not available, compinit still
completes the first time but cannot save the dump, so every new shell
rescans the library; the alias above avoids that by using zsh's own
built-in mv (zf_mv).

After compinit, Tab completion works for commands, options, file names
and so on.  With busybox on PATH, "busybox <TAB>" completes to the
applet names, and your own shims complete as ordinary commands.


Building from source
--------------------

The port builds with any mingw-w64 style cross compiler.  It was
developed with llvm-mingw (UCRT) and needs Python 3.  There is no
configure script; Src/win32/gen.py generates the files that configure
would normally produce and Src/win32/config.h holds the configuration.

On Linux (cross-compiling):

  export PATH=/path/to/llvm-mingw/bin:$PATH
  cd zsh
  make -f Src/win32/Makefile BUILDDIR=$PWD/build
  make -f Src/win32/Makefile BUILDDIR=$PWD/build install

or, from the top of this package, simply:

  ./build.sh              # builds build/zsh.exe
  ./build.sh --install    # also stages build/install
  ./build.sh --clean      # starts from scratch

(In this workspace llvm-mingw is installed in $HOME/.local/bin/llvm-mingw,
with symlinks for the x86_64-w64-mingw32-* tools in $HOME/.local/bin, so
"export PATH=$HOME/.local/bin:$PATH" is enough.  build.sh adds that to
PATH by itself.)

This produces:

  build/zsh.exe                 the executable
  build/install/...             a staged tree ready to copy to Windows

Copy the contents of build/install to C:\Program Files\zsh (or to the
prefix you built for).  "make install" only stages files; it does not
write to C:\ from Linux.

On Windows, install.bat in the top directory of the tree does the copy:
run it (or double-click it), approve the elevation prompt, and it
creates C:\Program Files\zsh and copies build\install into it.  It does
not change PATH; add C:\Program Files\zsh\bin yourself.

To test the result under Wine:

  wine build/zsh.exe -f -c 'print -r -- $ZSH_VERSION'
  wine build/zsh.exe -f -c 'print -r -- $((2**10))'

A complete build from scratch takes about 40 seconds on a typical
machine.  "make -f Src/win32/Makefile clean BUILDDIR=..." removes the
build directory.

More detail on the design and on the internals is in
Src/win32/README.md.  Known issues are in BUGS.txt.


Uninstalling
------------

  a. Remove the installation directory (C:\Program Files\zsh).
  b. Remove C:\Program Files\zsh\bin from PATH.
  c. Optionally delete your personal files:
       C:\Users\<you>\.zshrc, .zshenv, .zprofile, .zlogin, .zlogout,
       .zsh_history, .zcompdump*


Important: installing this does NOT make Linux programs run on Windows
----------------------------------------------------------------------

This is a shell, not an operating system.  It does not provide a Linux
kernel, a Linux filesystem, or a Linux syscall layer.  A Linux binary
(ELF format, .so libraries, /proc, /dev, ...) will not run just
because zsh is installed.  If you double-click a Linux executable on
Windows it will not work, and if you type "./my-linux-program" in zsh
it will not work either.

What DOES work:

  * Shell scripts written for zsh/bash/sh, as long as they only use
    commands that exist on your machine (Windows programs, zsh
    builtins, busybox applets, ...).

  * Windows programs: notepad, cmd, powershell, git, python, node,
    code, anything on PATH.  These are first-class citizens.

  * The Unix tools that busybox win64 provides (ls, cat, grep, sed,
    awk, find, sort, ...).  These are native Windows binaries that
    happen to implement Unix utilities.

What does NOT work:

  * Linux binaries.  They are a different format (ELF vs PE) and
    need a Linux kernel.  Use WSL for those.

  * Programs that depend on a Unix filesystem layout (/usr/bin, /etc,
    /dev, /proc, ...).  Windows has its own layout (C:\, D:\, ...).

  * Programs that depend on Unix signals, process groups, or Unix
    sockets in ways that Windows does not support.

If you are coming from Linux and are used to "apt install foo" giving
you a working program, the Windows equivalent is "winget install foo"
or downloading the Windows build of foo.  zsh does not change what
programs are available on your machine; it only changes the shell you
type commands into.


More information
----------------

  Version information: print -r -- $ZSH_VERSION
  Build documentation: Src/win32/README.md in the source tree
  Known issues: see the "Known issues" section below
  Installation guide: INSTALLATION.txt
  Tutorial for Windows users: TUTORIAL.txt
  Guide for Unix users: UNIXREADME.txt

The tutorial assumes you have never used a Unix shell.  The Unix guide
assumes you have used one for years and only want to know what is
different.  Read whichever one describes you; you will not need both.


Known issues in zsh for Windows (native port)
============================================

This file lists the limitations and known quirks of the native
Windows build of zsh (zsh.exe).  The first section covers things that
are genuinely unfinished or buggy in the port itself; the rest covers
behaviour that is expected, or that comes from the environment rather
than from the port.

-------------------------------------------------------------------------------
1. Known bugs in the port
-------------------------------------------------------------------------------

* Output without a trailing newline is erased by the next prompt
  --------------------------------------------------------------
  A command such as

      printf "hello"

  (no newline at the end) prints "hello", but when the next prompt is
  drawn the output line is wiped and "hello" disappears.  Commands that
  end their output with a newline (the common case) are unaffected.

  Root cause: zsh redraws the prompt by clearing the old line.  On a
  real terminal it clears to end of line in a way that leaves the cursor
  on the next line, so the previous output survives.  Under Wine the
  console does not track the cursor position correctly, and our port
  ends up clearing from column 0 of the same line, erasing the output.
  The workaround we use for the rest of the Wine console problems
  (widening the console buffer) does not help here.

  Status: isolated, not yet fixed.  Workaround: add a newline to the
  output, e.g.  printf "hello\n".

* Home, End and Delete are not bound by default
  ---------------------------------------------
  zsh does not bind these keys on any platform; they only produce a
  beep unless you bind them in .zshrc.  The Wine .zshrc installed by
  this port contains:

      bindkey '^[[H'  beginning-of-line
      bindkey '^[[F'  end-of-line
      bindkey '^[[3~' delete-char

  and INSTALLATION.txt section 5 gives the same lines for a real
  Windows install.  Omitting them is not a bug in the port.

* Arrow keys and Backspace misbehave on some Windows versions
  ----------------------------------------------------------
  On older Windows (before Windows 10 1607) the console does not
  support ENABLE_VIRTUAL_TERMINAL_PROCESSING or
  ENABLE_VIRTUAL_TERMINAL_INPUT.  Without VT processing the escape
  sequences the line editor sends are treated as literal text and the
  display falls apart; without VT input the arrow keys may be delivered
  as console events that the port cannot map.

  The port now disables VT input entirely (it reads key events via
  ReadConsoleInputW and maps VK codes to escape sequences itself) and
  enables VT processing for output.  On Windows 10 1607 and later this
  works correctly.  On older Windows the line editor will be limited.

  If Backspace appears to move the cursor forward instead of deleting,
  check that your .zshrc binds the editing keys (section above) and
  that you are on a supported Windows version.

* Wine's console does not understand escape sequences
  ---------------------------------------------------
  Wine's console passes the escape sequences through to the terminal
  (so the terminal draws them correctly) but counts every byte of them
  as a column.  Its idea of the cursor position therefore runs ahead of
  the real one, and once the count reaches the width of the console
  buffer Wine wraps the line itself and injects a newline into the
  output stream.  The port works around this by widening the console
  buffer to 4096 columns under Wine (detected via wine_get_version in
  ntdll.dll).  Nothing of this is visible on Windows itself, where the
  console interprets the sequences properly.

-------------------------------------------------------------------------------
2. Behaviour inherited from zsh / the environment
-------------------------------------------------------------------------------

* t2.zsh and t8.zsh return status 1
  --------------------------------
  These two test scripts intentionally exercise "bad substitution" and
  the failing command is their last one, so the script exits with status
  1.  This is expected and is not a regression.

* No job control
  --------------
  There are no Unix process groups or signals like SIGTSTP, so Ctrl+Z,
  "fg", "bg" and "%1" are not available.  Background jobs ("cmd &") and
  "wait" do work.

* No external utilities
  ---------------------
  This is the shell, not a collection of Unix tools: ls, cat, grep and
  friends do not exist unless something else on the machine provides
  them.  zsh's file builtins are available after "zmodload zsh/files":
  zf_mv, zf_rm, zf_rmdir, zf_mkdir, zf_ln, zf_chmod, zf_chown, zf_chgrp
  and zf_sync.

* No dynamic modules
  ------------------
  Everything is linked into zsh.exe.  Third-party modules cannot be
  loaded, but the modules listed in INSTALLATION.txt section 10 are
  included.

* fork() is emulated
  -------------------
  Subshells, pipelines, command substitution and process substitution all
  work, but starting one is slower than on Unix, and open file
  descriptors beyond 0, 1 and 2 do not survive.

* Byte based string handling
  --------------------------
  There is no MULTIBYTE_SUPPORT, as in a Unix zsh built without
  multibyte support.  UTF-8 output is passed through unchanged and is
  correct, but string handling and line editing are byte based:
  ${#string} counts bytes, and backspace at the end of a multi-byte
  character removes one byte of it.

* Tools with no Unix equivalent
  -----------------------------
  "head", "tail", "grep" and the rest are not present.  Use the
  Windows tools (findstr, more, ...) or load zsh's own builtins.

-------------------------------------------------------------------------------
3. Testing notes
-------------------------------------------------------------------------------

* The interactive tests (ctrltests.py, keys.py) in the development tree
  drive the shell through a pty under Wine.  They are the source of the
  "X> print ok" / "ok" / "X> exit" transcript used to verify the line
  editor.  Under a real Windows console the same behaviour is expected,
  and the escape-sequence handling is the console's own, which is
  correct there.

* The 12 test suites in zsh/Test were run through the native build under
  Wine; only t2.zsh and t8.zsh exit with status 1, and that is by
  design (see above).
Behind the Scenes: how zsh for Windows works
============================================

This document is for people who are curious about how a Unix shell ended up
running natively on Windows without any POSIX emulation layer.  It is not
required reading to use the shell; INSTALLATION.txt covers that.  It is
written for two audiences at once: Windows users who want to understand
what they just installed, and Unix users who want to know how their
favourite shell survived the trip.


1. What "native" actually means here
------------------------------------

Most ports of Unix software to Windows fall into one of two camps:

  * A POSIX emulation layer (Cygwin, MSYS2, WSL).  The program is
    compiled against a Unix-like API that the layer provides; the layer
    translates calls to Win32 at runtime.  This works well but adds a
    DLL, a runtime, and a layer of indirection, and programs behave
    slightly differently from "real" Windows programs.

  * A "native" port that removes the Unix assumptions one by one and
    rebuilds the program on top of Win32.  This is harder but produces
    a single executable that behaves like any other Windows program.

zsh.exe is the second kind.  It is a console application that links only
against KERNEL32.dll, bcrypt.dll and the UCRT.  There is no cygwin1.dll,
no msys-2.0.dll, no WSL interop.  Everything zsh needs from a Unix
system is reimplemented on top of the Win32 API in Src/win32/.

The port is not a fork of zsh with the Unix parts ripped out; it is the
real zsh source tree with a Win32 compatibility layer glued underneath.
The core (parser, expansion, builtins, completion, the line editor) is
unchanged upstream code.  What changed is the platform layer: how zsh
reads the terminal, how it spawns processes, how it finds files, how it
measures time, and how it talks to the operating system.

2. The big pieces
-----------------

  Src/win32/compat.c      Unix compatibility shims: fork, wait, signals,
                         pipes, tty ioctls, file modes, path
                         translation, getcwd, realpath, ...

  Src/win32/term.c       Terminal handling.  This is where most of the
                         interesting work lives; see section 3.

  Src/win32/termcap.c    A tiny termcap database and tgoto() expander,
                         because Windows has no termcap/terminfo.

  Src/win32/signal.c     Signal delivery, since Windows has no signals
                         in the Unix sense.

  Src/win32/passwd.c     User and home-directory lookup ($HOME, $USER,
                         $USERNAME, the password database).

  Src/win32/zshwin32.c   Process spawning, job control stubs, the
                         multiple-redirection helper, and the entry
                         point glue.

  Src/win32/gen.py       Replaces autoconf: generates config.h,
                         objects.mk and the module list without a
                         configure run.

  Src/win32/Makefile     A self-contained build that drives gen.py and
                         then compiles everything with a mingw-w64
                         cross compiler.

  Src/win32/README.md    The maintainer's notes on the design and on the
                         internals.  INSTALLATION.txt is the user-facing
                         version of the same material.

3. The terminal: the hard part
------------------------------

Unix zsh assumes a terminal is a single file descriptor that can be
read from and written to, configured with termios, and described by
terminfo.  Windows splits all of this into separate objects:

  * The console has a separate input handle (CONIN$) and output handle
    (CONOUT$).  zsh's SHTTY is the input handle; the prompt stream
    "shout" and the write_loop() calls used by the line editor go to
    the output handle instead.  init_shout() in Src/init.c picks the
    output handle (preferring stderr, which is already the console).

  * Virtual terminal processing has to be enabled explicitly
    (ENABLE_VIRTUAL_TERMINAL_PROCESSING) before the console will
    interpret escape sequences.  zsh_win32_init_console() does that and
    switches both directions to UTF-8, restoring the previous settings
    on exit.

  * Console input is not a byte stream.  In cooked mode the port uses
    ReadConsoleW; in raw mode it uses ReadConsoleInputW and translates
    KEY_EVENT records into the byte sequences a Unix terminal would
    produce (arrow keys become ESC [ A, Home becomes ESC [ H, and so
    on).  A bare Alt key press is turned into ESC, because Wine turns
    a Unix terminal's ESC into an Alt key press.

  * There is no termcap database.  termcap.c supplies the capabilities
    zsh asks for (le, nd, up, do, dc, cd, ce, md, ch, ...) as the
    escape sequences of a virtual terminal, and tgoto() expands the
    "%p1%d" parameters so that cursor motion uses the intended
    sequences.  The capability names are the termcap names ("cd", "ce",
    "le", ...), because the port does not use terminfo at all.

  * The tgoto() implementation is a real parameterised-capability
    expander, not the simplistic one the port started with.  Termcap
    uses "%p1" to push a parameter and a separate "%d" to print it, so
    "%p1%d" is two directives, not one.  Getting this wrong produced
    literal "%d" in the output and corrupted the redraw.

4. The console of Wine does not understand escape sequences
-----------------------------------------------------------

This is the bug that took the longest to track down, and it is worth
explaining because it is not a zsh bug and not a Wine bug in the usual
sense; it is a mismatch between two different ideas of what a console is.

On Windows, the console is a screen buffer with a cursor.  When virtual
terminal processing is on, the console interprets the escape sequences
that programs write and updates the buffer and the cursor in step.  The
program and the console agree on where the cursor is.

Wine's console does not do that.  It passes the escape sequences through
to the terminal (so the terminal draws them correctly) but counts every
single byte of them as a column when tracking its own cursor position.
The count therefore runs ahead of the real cursor, and once it reaches
the width of the console buffer Wine wraps the line itself and injects
a newline into the output stream.  That newline lands in the middle of
the line being edited and the display falls apart: "print ok" becomes
"print o" on one line and "k" on the next, Ctrl-U leaves fragments
behind, and so on.

The port's workaround is in zsh_win32_widen_console_buffer(): under Wine
(detected with wine_get_version() in ntdll.dll, which no real Windows
system has) the console buffer is made much wider than the visible
window (4096 columns instead of 80).  The miscount then needs thousands
of bytes before it can reach the margin, and it starts again at every
prompt, because the shell begins a new line with a carriage return.
Nothing of this is visible on Windows itself, where the console
interprets the sequences properly.

A second, narrower consequence of the same problem is documented in
BUGS.txt: after a command whose output does not end with a newline, the
output line can be erased when the next prompt is drawn.  This is a
known limitation and is being left for a later pass.

5. Process model
----------------

Windows has no fork().  The port emulates it by starting a new zsh.exe
that restores the parent's state.  Subshells, pipelines, command
substitution and process substitution all work, but starting one is
slower than on Unix, and open file descriptors beyond 0, 1 and 2 do
not survive.

Signals are polled at safe points rather than delivered asynchronously,
and job control (Ctrl+Z, fg, bg, %1) is not available, because Windows
has no process groups in the Unix sense.  Background jobs ("cmd &") and
"wait" do work.

Windows programs can be named without their extension, as in cmd.exe:
"cmd", "notepad" and so on are found through PATHEXT.  The usual form
with ".exe" works as well.

6. Files and paths
------------------

The shell assumes that an absolute path starts with a slash.  On
Windows that is not true: "C:/foo" and "//server/share" are absolute as
well, so the port uses ZSH_ISABSPATH() (implemented by
zsh_win32_isabs()) where the core code used to test for a leading
slash.

Paths inside the shell are written with forward slashes; backslashes
are accepted when you type them, but "C:/Users/me" is the form zsh
reports back.  The directory of $HOME is reported the same way.  PATH
is a normal Windows PATH: the semicolon-separated value is split into
the $path array, and assigning to $path (or $PATH) works in both
directions.

File modes are emulated: stat() and fstat() replace the mode bits the
C runtime reports with plausible POSIX bits (0755 for directories, 0644
for files, 0555/0444 for read-only objects, plus the execute bits for
.exe, .com, .bat, .cmd and .ps1 files).  Windows permissions are ACLs
and cannot be expressed this way, but scripts (and compaudit) do look
at the bits.

7. Building
-----------

The port does not use autoconf.  Src/win32/gen.py generates the files
that configure would normally produce (config.h, objects.mk, the module
list) and Src/win32/config.h holds the configuration.  The build is
driven by Src/win32/Makefile, or by build.sh in the root of this
package, which finds llvm-mingw under ~/.local/bin, runs the Makefile
with BUILDDIR=build, and produces build/zsh.exe.  A complete build
from scratch takes about 40 seconds.

The toolchain is permanently installed under ~/.local/bin/llvm-mingw,
with symlinks for the x86_64-w64-mingw32-* tools in ~/.local/bin, so
no extra PATH setup is needed.

8. Credits
----------

zsh itself is the work of the Zsh Development Group and many
contributors; this port would not exist without that codebase.

The port was developed with the help of OpenCode (https://opencode.ai),
the coding agent harness used to explore the source tree, reproduce
bugs under Wine, instrument the line editor, and iterate on the fixes.
OpenCode's tooling (file editing, shell execution, pty harnesses,
web access) made it practical to chase down the Wine console bug and
the termcap bugs that a manual port would have left undiscovered.
