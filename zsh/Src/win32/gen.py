#!/usr/bin/env python3
"""Generate the derived source files needed to build zsh natively for Windows.

The upstream zsh build relies on autoconf/config.status and a rather
intricate set of shell scripts (mkmakemod.sh, mkbltnmlst.sh, ...) to
produce the files that the compiler needs:

  * <dir>/<file>.syms / .epro / .pro   -- prototype lists extracted from
    the sources by Src/makepro.awk
  * <dir>/<module>.mdh                 -- per-module header
  * Src/zsh.mdh                        -- header of the main module
  * Src/bltinmods.list                 -- list of built-in modules
  * Src/signames.c, Src/sigcount.h     -- signal name tables
  * Src/version.h, Src/patchlevel.h
  * Src/zshpaths.h, Src/zshxmods.h

This script reproduces all of that for a hand-rolled Windows build; it is
run by Src/win32/Makefile.

Usage:  gen.py <source-tree-root> <build-directory>
"""

import os
import re
import shutil
import subprocess
import sys

VERSION = "5.9.999.3-test"

# Modules that need extra generated headers of their own.
EXTRA_GEN = {}


def qname(name):
    """Turn a module name into the C identifier used by zsh."""
    return name.replace("Q", "Qq").replace("_", "Qu").replace("/", "Qs")


class Module(object):
    def __init__(self, name, mddfile, srcdir):
        self.name = name
        self.mddfile = mddfile
        self.qname = qname(name)
        self.srcdir = srcdir          # e.g. "Src", "Src/Zle", "Src/Modules"
        self.basename = os.path.basename(mddfile)[:-4]
        self.objects = []
        self.headers = []
        self.moddeps = []
        self.nozshdep = False
        self.alwayslink = False
        self.hdrdeps = []
        self.otherincs = []
        self.autofeatures = []
        self.autofeatures_emu = []
        self.load = "yes"
        self.functions = ""


def parse_mdd(srcdir, mddfile):
    """Parse a .mdd file (a shell script) with a tiny shell evaluator."""
    path = os.path.join(srcdir, mddfile)
    with open(path, "r") as f:
        text = f.read()

    # Strip the embedded make fragment.
    text = re.sub(r":<<\\Make\n.*?\nMake\n", "", text, flags=re.S)

    # Join lines ending in a backslash (shell line continuation).
    text = re.sub(r"\\\n\s*", " ", text)

    env = {}
    # We only support the simple `name=value` / `name='value'` forms that the
    # .mdd files use for the fields we are interested in.
    for m in re.finditer(
            r"^([A-Za-z_][A-Za-z_0-9]*)=('([^']*)'|\"([^\"]*)\"|([^\n]*))$",
            text, re.M):
        key = m.group(1)
        val = m.group(3)
        if val is None:
            val = m.group(4)
        if val is None:
            val = m.group(5)
        env[key] = val.strip()

    mod = Module(env.get("name", ""), mddfile, os.path.dirname(mddfile))
    mod.objects = env.get("objects", "").split()
    mod.headers = env.get("headers", "").split()
    mod.moddeps = env.get("moddeps", "").split()
    mod.nozshdep = bool(env.get("nozshdep", ""))
    # Upstream: a module depends on the main module unless it says
    # otherwise.  This is what makes the module header pull in the
    # headers of the shell itself.
    if not mod.nozshdep and "zsh/main" not in mod.moddeps:
        mod.moddeps.append("zsh/main")
    mod.alwayslink = bool(env.get("alwayslink", ""))
    mod.hdrdeps = env.get("hdrdeps", "").split()
    mod.otherincs = env.get("otherincs", "").split()
    mod.autofeatures = env.get("autofeatures", "").split()
    mod.autofeatures_emu = env.get("autofeatures_emu", "").split()
    mod.load = env.get("load", "yes")
    mod.functions = env.get("functions", "")
    return mod


def find_mdd(srcdir, name):
    for sub in ("Src", "Src/Zle", "Src/Modules", "Src/Builtins"):
        d = os.path.join(srcdir, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".mdd"):
                continue
            m = parse_mdd(srcdir, os.path.join(sub, f))
            if m.name == name:
                return m
    raise SystemExit("gen.py: no .mdd file found for module %s" % name)


def mkdirs(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        raise SystemExit("gen.py: command failed: %s" % " ".join(cmd))


def gen_protos(srcdir, builddir, mod, subdirname, extra=None):
    """Run makepro.awk over each source file of the module."""
    outdir = os.path.join(builddir, mod.srcdir)
    mkdirs(outdir)
    awk = os.path.join(srcdir, "Src", "makepro.awk")
    objs = list(mod.objects)
    if extra:
        objs = objs + [e + ".o" for e in extra]
    for obj in objs:
        base = obj[:-2] if obj.endswith(".o") else obj
        src = os.path.join(srcdir, mod.srcdir, base + ".c")
        if not os.path.exists(src):
            # Maybe a generated source (errnames.c), which gen.py has
            # already written to the build directory.
            src = os.path.join(builddir, mod.srcdir, base + ".c")
        if not os.path.exists(src):
            # Generated source (signames.c); handled separately.
            continue
        syms = os.path.join(outdir, base + ".syms")
        with open(syms, "w") as out:
            run(["awk", "-f", awk, src, subdirname], stdout=out)
        with open(syms) as f:
            lines = f.read().splitlines()
        epro = [l[1:] for l in lines if l.startswith("E")]
        pro = [l[1:] for l in lines if l.startswith("L")]
        with open(os.path.join(outdir, base + ".epro"), "w") as f:
            f.write("/* Generated automatically */\n")
            if epro:
                f.write("\n".join(epro) + "\n")
        with open(os.path.join(outdir, base + ".pro"), "w") as f:
            f.write("/* Generated automatically */\n")
            if pro:
                f.write("\n".join(pro) + "\n")


def gen_errnames(srcdir, builddir):
    """Generate errnames.c / errcount.h for the zsh/system module.

    The table maps the error numbers of the C library to the names it
    gives them.  The names and values are read from the compiler's own
    <errno.h>, so they always agree with what the shell sees at run
    time; where several names share a value the first one wins, as in
    the upstream build.
    """
    moddir = os.path.join(builddir, "Src", "Modules")
    mkdirs(moddir)
    cc = os.environ.get("CC", "x86_64-w64-mingw32-clang")

    defs = {}
    try:
        r = subprocess.run([cc, "-E", "-dM", "-x", "c", "-"],
                           input=b"#include <errno.h>\n",
                           stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            for line in r.stdout.decode("utf-8", "replace").splitlines():
                m = re.match(r"#define\s+(E[A-Z0-9_]+)\s+(\S+)\s*$", line)
                if m:
                    defs[m.group(1)] = m.group(2)
    except OSError:
        pass

    bynum = {}
    for name, val in defs.items():
        hops = 0
        while val in defs and hops < 16:
            val = defs[val]
            hops += 1
        if re.match(r"^(0[xX][0-9a-fA-F]+|[0-9]+)$", val):
            num = int(val, 0)
            if num not in bynum:
                bynum[num] = name
    named = [n for n in bynum if 0 < n < 1024]
    maxnum = max(named) if named else 0

    with open(os.path.join(moddir, "errnames.c"), "w") as f:
        f.write("/** errnames.c                                 **/\n")
        f.write("/** architecture-customized errnames.c for zsh **/\n")
        f.write("\n")
        f.write("#define ERRCOUNT\t%d\n" % maxnum)
        f.write("\n")
        f.write('#include "system.mdh"\n')
        f.write("\n")
        f.write("/**/\n")
        f.write("const char *sys_errnames[ERRCOUNT+1] = {\n")
        for i in range(1, maxnum + 1):
            f.write('\t"%s",\n' % bynum.get(i, "E%d" % i))
        f.write("\tNULL\n};")
    with open(os.path.join(moddir, "errcount.h"), "w") as f:
        f.write("#define ERRCOUNT\t%d\n" % maxnum)


def mdh_text(mod, deps, outdir):
    """Produce the contents of a module's .mdh file."""
    L = []
    L.append("#ifndef have_%s_module" % mod.qname)
    L.append("#define have_%s_module" % mod.qname)
    L.append("")
    L.append("# ifndef IMPORTING_MODULE_%s" % mod.qname)
    L.append("#  ifndef MODULE")
    L.append("#   define boot_ boot_%s" % mod.qname)
    L.append("#   define cleanup_ cleanup_%s" % mod.qname)
    L.append("#   define features_ features_%s" % mod.qname)
    L.append("#   define enables_ enables_%s" % mod.qname)
    L.append("#   define setup_ setup_%s" % mod.qname)
    L.append("#   define finish_ finish_%s" % mod.qname)
    L.append("#  endif /* !MODULE */")
    L.append("# endif /* !IMPORTING_MODULE_%s */" % mod.qname)
    L.append("")
    if deps:
        L.append("/* Module dependencies */")
        for dep, path in deps:
            L.append("# define IMPORTING_MODULE_%s 1" % dep.qname)
            L.append('# include "%s"' % path)
        L.append("")
    if mod.headers:
        L.append("/* Extra headers for this module */")
        for h in mod.headers:
            L.append('# include "%s"' % h)
        L.append("")
    L.append("# undef mod_import_variable")
    L.append("# undef mod_import_function")
    L.append("# if defined(IMPORTING_MODULE_%s) &&  defined(MODULE)" % mod.qname)
    L.append("#  define mod_import_variable ")
    L.append("#  define mod_import_function ")
    L.append("# else")
    L.append("#  define mod_import_function")
    L.append("#  define mod_import_variable")
    L.append("# endif /* IMPORTING_MODULE_%s && MODULE */" % mod.qname)
    for obj in mod.objects:
        base = obj[:-2] if obj.endswith(".o") else obj
        L.append('# include "%s.epro"' % base)
    for inc in mod.otherincs:
        L.append('# include "%s"' % inc)
    L.append("# undef mod_import_variable")
    L.append("# define mod_import_variable")
    L.append("# undef mod_import_variable")
    L.append("# define mod_import_variable")
    L.append("# ifndef mod_export")
    L.append("#  define mod_export ")
    L.append("# endif /* mod_export */")
    L.append("")
    L.append("#endif /* !have_%s_module */" % mod.qname)
    L.append("")
    return "\n".join(L)


def gen_zle_headers(srcdir, builddir):
    """Reproduce the zle_things.h / zle_widget.h generation of zle.mdd."""
    zledir = os.path.join(srcdir, "Src", "Zle")
    outdir = os.path.join(builddir, "Src", "Zle")
    mkdirs(outdir)
    with open(os.path.join(zledir, "iwidgets.list")) as f:
        raw = f.read().splitlines()

    entries = []
    for line in raw:
        line = line.split("#")[0].strip()
        if not line:
            continue
        m = re.match(r'^"([^"]*)"\s*,\s*([^,]*)\s*,\s*(.*)$', line)
        if not m:
            continue
        entries.append((m.group(1), m.group(2).strip(), m.group(3).strip()))

    # The thingies: every widget has an ordinary thingy and an immortal
    # one whose name starts with a dot.  The identifiers are the name
    # with the dashes removed; the names keep them.  As upstream does,
    # all the ordinary thingies come first, then the immortal ones.
    thingies = []
    immortals = []
    widgets = []
    for name, func, flags in entries:
        nn = name.replace("-", "")
        thingies.append('T("%s", 0, w_%s, t_D%s)' % (name, nn, nn))
        immortals.append('T(".%s", TH_IMMORTAL, w_%s, t_%s)' % (name, nn, nn))
        widgets.append("W(%s, t_%s, %s)" % (flags, nn, func))
    thingies += immortals

    with open(os.path.join(outdir, "thingies.list"), "w") as f:
        f.write("/** thingies.list                            **/\n")
        f.write("/** thingy structures for the known thingies **/\n")
        f.write("\n")
        f.write("/* format: T(\"name\", TH_FLAGS, w_widget, t_nextthingy) */\n")
        f.write("\n")
        f.write("\n".join(thingies) + "\n")
    with open(os.path.join(outdir, "widgets.list"), "w") as f:
        f.write("/** widgets.list                               **/\n")
        f.write("/** widget structures for the internal widgets **/\n")
        f.write("\n")
        f.write("/* format: W(ZLE_FLAGS, t_firstname, functionname) */\n")
        f.write("\n")
        f.write("\n".join(widgets) + "\n")

    # zle_things.h
    out = []
    out.append("/** zle_things.h                              **/")
    out.append("/** indices of and pointers to known thingies **/")
    out.append("")
    out.append("enum {")
    for t in thingies:
        m = re.match(r'T\("([^"]*)"', t)
        nm = m.group(1).replace("-", "").replace(".", "D")
        out.append("    z_%s," % nm)
        out.append("#define t_%s (&thingies[z_%s])" % (nm, nm))
    out.append("    ZLE_BUILTIN_THINGY_COUNT")
    out.append("};")
    with open(os.path.join(outdir, "zle_things.h"), "w") as f:
        f.write("\n".join(out) + "\n")

    # zle_widget.h
    out = []
    out.append("/** zle_widget.h                                **/")
    out.append("/** indices of and pointers to internal widgets **/")
    out.append("")
    out.append("enum {")
    for w in widgets:
        m = re.match(r'W\([^,]*,\s*t_([^,]*),', w)
        nm = m.group(1).strip()
        out.append("    wi_%s," % nm)
        out.append("#define w_%s (&widgets[wi_%s])" % (nm, nm))
    out.append("    ZLE_BUILTIN_WIDGET_COUNT")
    out.append("};")
    with open(os.path.join(outdir, "zle_widget.h"), "w") as f:
        f.write("\n".join(out) + "\n")


def read_signal_table(srcdir):
    """Read the signal table from the Win32 signal.h shim."""
    path = os.path.join(srcdir, "Src", "win32", "include", "signal.h")
    table = []
    with open(path) as f:
        for line in f:
            m = re.match(r"^#define\s+(SIG[A-Z0-9]+)\s+([0-9]+)\s*$", line)
            if m:
                table.append((m.group(1), int(m.group(2))))
    # Sort by number, then by name.
    table.sort(key=lambda x: (x[1], x[0]))
    return table


MESSAGES = {
    "SIGHUP": "hangup",
    "SIGINT": "interrupt",
    "SIGQUIT": "quit",
    "SIGILL": "illegal hardware instruction",
    "SIGTRAP": "trace trap",
    "SIGABRT": "abort",
    "SIGEMT": "EMT instruction",
    "SIGFPE": "floating point exception",
    "SIGKILL": "killed",
    "SIGBUS": "bus error",
    "SIGSEGV": "segmentation fault",
    "SIGSYS": "invalid system call",
    "SIGPIPE": "broken pipe",
    "SIGALRM": "alarm",
    "SIGTERM": "terminated",
    "SIGBREAK": "break",
    "SIGCHLD": "death of child",
    "SIGCONT": "continued",
    "SIGSTOP": "stopped",
    "SIGTSTP": "stopped",
    "SIGWINCH": "window size changed",
    "SIGUSR1": "user-defined signal 1",
    "SIGUSR2": "user-defined signal 2",
}


def write_signames(srcdir, path):
    """Write the signames.c file (equivalent of signames1.awk/signames2.awk)."""
    table = read_signal_table(srcdir)
    if not table:
        raise SystemExit("gen.py: no signals found in signal.h shim")
    maxsig = max(n for _, n in table)
    bynum = {}
    for name, num in table:
        bynum.setdefault(num, name)

    out = []
    out.append("/** signames.c                                 **/")
    out.append("/** architecture-customized signames.c for zsh **/")
    out.append("")
    out.append("#define SIGCOUNT\t%d" % maxsig)
    out.append("")
    out.append('#include "zsh.mdh"')
    out.append("")
    out.append("/**/")
    out.append("mod_export char *sig_msg[SIGCOUNT+2] = {")
    out.append('\t"done",')
    for i in range(1, maxsig + 1):
        name = bynum.get(i)
        if name is None:
            out.append('\t"",')
        elif name in ("SIGSTOP", "SIGTSTP"):
            out.append("# ifdef USE_SUSPENDED")
            out.append('\t"suspended (signal)",')
            out.append("# else")
            out.append('\t"stopped (signal)",')
            out.append("# endif")
        else:
            out.append('\t"%s",' % MESSAGES.get(name, name))
    out.append("\tNULL")
    out.append("};")
    out.append("")
    out.append("/**/")
    out.append("char *sigs[SIGCOUNT+4] = {")
    out.append('\t"EXIT",')
    for i in range(1, maxsig + 1):
        name = bynum.get(i)
        if name is None:
            out.append('\t"%d",' % i)
        else:
            # The shell looks signals up by their bare names, e.g. INT.
            out.append('\t"%s",' % name[3:])
    out.append('\t"ZERR",')
    out.append('\t"DEBUG",')
    out.append("\tNULL")
    out.append("};")
    out.append("")
    with open(path, "w") as f:
        f.write("\n".join(out))


def relpath_to_mdhs(mod, modules, srcdir=None):
    """Return the list of (dependency module, include path) for a module."""
    deps = []
    for d in mod.moddeps:
        dm = modules.get(d)
        if dm is None:
            # A dependency that is not built here; we still need to know
            # where its header lives so that the include works.
            dm = find_mdd(srcdir, d)
        if dm.srcdir == mod.srcdir:
            path = dm.basename + ".mdh"
        else:
            # relative path from build/<mod.srcdir> to build/<dm.srcdir>
            here = mod.srcdir.split("/")
            there = dm.srcdir.split("/")
            n = 0
            while n < len(here) and n < len(there) and here[n] == there[n]:
                n += 1
            up = [".."] * (len(here) - n)
            path = "/".join(up + there[n:] + [dm.basename + ".mdh"])
        deps.append((dm, path))
    return deps


def module_dep_dirs(mod, modules, srcdir):
    """Return the source directories of the modules `mod` depends on."""
    seen = []
    stack = list(mod.moddeps)
    while stack:
        d = stack.pop(0)
        dm = modules.get(d)
        if dm is None:
            try:
                dm = find_mdd(srcdir, d)
            except SystemExit:
                continue
        if dm.srcdir not in seen:
            seen.append(dm.srcdir)
        stack.extend(dm.moddeps)
    return seen


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    srcdir = os.path.abspath(sys.argv[1])
    builddir = os.path.abspath(sys.argv[2])

    with open(os.path.join(srcdir, "Src", "win32", "modules.txt")) as f:
        names = []
        for line in f:
            line = line.split("#")[0].strip()
            if line:
                names.append(line)

    modules = {}
    order = []
    for n in names:
        m = find_mdd(srcdir, n)
        modules[n] = m
        order.append(m)

    mkdirs(builddir)
    mkdirs(os.path.join(builddir, "Src"))

    # ---- config.h -------------------------------------------------------
    shutil.copyfile(os.path.join(srcdir, "Src", "win32", "config.h"),
                    os.path.join(builddir, "config.h"))

    # ---- config.modules -------------------------------------------------
    with open(os.path.join(builddir, "config.modules"), "w") as f:
        f.write("# Automatically generated by Src/win32/gen.py\n")
        for m in order:
            f.write("name=%s modfile=%s link=static auto=yes load=%s%s\n" %
                    (m.name, m.mddfile, m.load,
                     (" functions=" + m.functions) if m.functions else ""))

    # ---- prototypes -----------------------------------------------------
    gen_errnames(srcdir, builddir)
    for m in order:
        gen_protos(srcdir, builddir, m, m.srcdir)

    # main.c lives outside the module object list but still needs its
    # prototype file.
    mainmod = modules["zsh/main"]
    gen_protos(srcdir, builddir, mainmod, mainmod.srcdir, extra=["main"])

    # ---- module headers -------------------------------------------------
    for m in order:
        outdir = os.path.join(builddir, m.srcdir)
        mkdirs(outdir)
        deps = relpath_to_mdhs(m, modules, srcdir)
        with open(os.path.join(outdir, m.basename + ".mdh"), "w") as f:
            f.write(mdh_text(m, deps, outdir))

    # ---- zle specific generated headers ---------------------------------
    if "zsh/zle" in modules:
        gen_zle_headers(srcdir, builddir)

    # ---- object list ----------------------------------------------------
    with open(os.path.join(builddir, "objects.mk"), "w") as f:
        f.write("# Automatically generated by Src/win32/gen.py\n")
        for m in order:
            objs = list(m.objects)
            if m.name == "zsh/main":
                objs = objs + ["main.o"]
            for obj in objs:
                base = obj[:-2] if obj.endswith(".o") else obj
                src = os.path.join(srcdir, m.srcdir, base + ".c")
                if not os.path.exists(src):
                    # generated source (e.g. signames.c)
                    gen = os.path.join(builddir, m.srcdir, base + ".c")
                    f.write("$(BUILDDIR)/%s/%s.o: $(BUILDDIR)/%s/%s.c\n" %
                            (m.srcdir, base, m.srcdir, base))
                    f.write("\t$(CC) $(ALL_CPPFLAGS) $(CFLAGS) -c -o $@ $<\n")
                f.write("OBJS += $(BUILDDIR)/%s/%s.o\n" % (m.srcdir, base))
            f.write("$(BUILDDIR)/%s/%%.o: EXTRA_CPPFLAGS += "
                    "-I$(BUILDDIR)/%s -I$(BUILDDIR)/Src -I$(SRCDIR)/%s\n" %
                    (m.srcdir, m.srcdir, m.srcdir))
            if m.srcdir != "Src":
                f.write("$(BUILDDIR)/%s/%%.o: EXTRA_CPPFLAGS += "
                        "-I$(SRCDIR)/Src\n" % m.srcdir)
            # A module sees the headers of the modules it depends on.
            for dep in module_dep_dirs(m, modules, srcdir):
                f.write("$(BUILDDIR)/%s/%%.o: EXTRA_CPPFLAGS += "
                        "-I$(SRCDIR)/%s\n" % (m.srcdir, dep))

        # The Win32 compatibility layer.  These are not zsh modules: they
        # are plain C files that implement the Unix interfaces zsh needs
        # directly on top of the Win32 API.
        win32objs = ["compat", "signal", "term", "passwd", "proc",
                     "termcap", "zshwin32"]
        for base in win32objs:
            src = os.path.join(srcdir, "Src", "win32", base + ".c")
            if not os.path.exists(src):
                continue
            f.write("OBJS += $(BUILDDIR)/Src/win32/%s.o\n" % base)
        f.write("$(BUILDDIR)/Src/win32/%%.o: EXTRA_CPPFLAGS += "
                "-I$(BUILDDIR)/Src -I$(SRCDIR)/Src\n")

    # ---- bltinmods.list -------------------------------------------------
    env = dict(os.environ)
    env["srcdir"] = os.path.join(srcdir, "Src")
    env["CFMOD"] = os.path.join(builddir, "config.modules")
    bltin = os.path.join(builddir, "Src", "bltinmods.list")
    with open(bltin, "w") as out:
        run(["sh", os.path.join(srcdir, "Src", "mkbltnmlst.sh"), bltin],
            env=env, stdout=out)

    # ---- signal tables --------------------------------------------------
    signames = os.path.join(builddir, "Src", "signames.c")
    write_signames(srcdir, signames)
    with open(os.path.join(builddir, "Src", "sigcount.h"), "w") as f:
        with open(signames) as sf:
            for line in sf:
                if "SIGCOUNT" in line and line.startswith("#define"):
                    f.write(line)
                    break

    # prototypes for the generated signames.c
    awk = os.path.join(srcdir, "Src", "makepro.awk")
    with open(os.path.join(builddir, "Src", "signames.syms"), "w") as out:
        run(["awk", "-f", awk, signames, "Src"], stdout=out)
    with open(os.path.join(builddir, "Src", "signames.syms")) as f:
        lines = f.read().splitlines()
    with open(os.path.join(builddir, "Src", "signames.epro"), "w") as f:
        f.write("/* Generated automatically */\n")
        f.write("\n".join(l[1:] for l in lines if l.startswith("E")) + "\n")
    with open(os.path.join(builddir, "Src", "signames.pro"), "w") as f:
        f.write("/* Generated automatically */\n")
        f.write("\n".join(l[1:] for l in lines if l.startswith("L")) + "\n")

    # ---- version information --------------------------------------------
    with open(os.path.join(builddir, "Src", "version.h"), "w") as f:
        f.write('#define ZSH_VERSION "%s"\n' % VERSION)
    with open(os.path.join(builddir, "Src", "patchlevel.h"), "w") as f:
        f.write('#define ZSH_PATCHLEVEL "%s"\n' % VERSION)

    # ---- installation paths ---------------------------------------------
    prefix = os.environ.get("ZSH_WIN32_PREFIX", r"C:\Program Files\zsh")
    prefix = prefix.replace("\\", "/").rstrip("/")

    def cpath(rel):
        """A path below the installation prefix as a C string literal.

        The shell's path handling only understands forward slashes, so
        those are used throughout, even though Windows accepts both.
        """
        return (prefix + rel.replace("\\", "/")).replace('"', '\\"')

    with open(os.path.join(builddir, "Src", "zshpaths.h"), "w") as f:
        f.write('#define MODULE_DIR "%s"\n' % cpath("/lib/zsh"))
        f.write('#define FPATH_DIR "%s"\n' % cpath("/share/zsh/functions"))
        f.write('#define SITEFPATH_DIR "%s"\n' % cpath("/share/zsh/site-functions"))
        f.write('#define SCRIPT_DIR "%s"\n' % cpath("/share/zsh/scripts"))
        f.write('#define SITESCRIPT_DIR "%s"\n' % cpath("/share/zsh/scripts"))
        f.write('#define ADDITIONAL_FPATH { "." }\n')

        # The directories below FPATH_DIR that hold autoloadable functions.
        # configure works these out from config.modules; here the source
        # tree already tells us, so walk it.  Completion functions are
        # installed one directory higher than they live in the source tree
        # (Completion/Unix/Command/_foo becomes Completion/Unix/_foo).
        subdirs = set()
        fnroot = os.path.join(srcdir, "Functions")
        if os.path.isdir(fnroot):
            for root, dirs, files in os.walk(fnroot):
                rel = os.path.relpath(root, fnroot).replace(os.sep, "/")
                if rel == "." or not files:
                    continue
                if rel.split("/")[0] == "Scripts":
                    continue
                subdirs.add(rel)
        cproot = os.path.join(srcdir, "Completion")
        if os.path.isdir(cproot):
            for root, dirs, files in os.walk(cproot):
                rel = os.path.relpath(root, cproot).replace(os.sep, "/")
                if rel == ".":
                    if files:
                        subdirs.add("Completion")
                    continue
                if files:
                    subdirs.add("Completion/" + rel.split("/")[0])
        f.write('#define FPATH_SUBDIRS { %s }\n' %
                ", ".join('"%s"' % d for d in sorted(subdirs)))

    # ---- linked module list ---------------------------------------------
    with open(os.path.join(builddir, "Src", "zshxmods.h"), "w") as f:
        for m in order:
            if m.name == "zsh/main":
                continue
            f.write("#define LINKED_XMOD_%s 1\n" % m.qname)

    # ---- misc empty headers ---------------------------------------------
    with open(os.path.join(builddir, "Src", "zshcurses.h"), "w") as f:
        f.write("")
    with open(os.path.join(builddir, "Src", "zshterm.h"), "w") as f:
        f.write("")

    print("gen.py: generated build files in %s" % builddir)


if __name__ == "__main__":
    main()
