#!/bin/sh
#
# build.sh - build the native Windows zsh (zsh.exe) from this source tree.
#
# The build produces:
#
#     build/zsh.exe          the shell (64 bit Windows, statically linked)
#     build/install/...      a staged installation tree (with --install)
#
# Usage:
#
#     ./build.sh                 build build/zsh.exe
#     ./build.sh --clean         remove the build directory first
#     ./build.sh --install       also stage build/install (bin/ and share/)
#     ./build.sh --prefix DIR    install prefix compiled into the binary
#                                (default: C:\Program Files\zsh)
#     ./build.sh --jobs N        number of parallel compile jobs
#
# The cross compiler is llvm-mingw.  It is expected in
# $HOME/.local/bin (either directly or as $HOME/.local/bin/llvm-mingw);
# set CC to use a different one.

set -e

root=$(cd "$(dirname "$0")" && pwd)
src="$root/zsh"
build="$root/build"

clean=no
install=no
prefix=
jobs=

usage() {
    sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)   clean=yes; shift ;;
        --install) install=yes; shift ;;
        --prefix)  prefix=$2; shift 2 ;;
        --jobs)    jobs=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "build.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

# The toolchain: llvm-mingw, installed under ~/.local/bin.
if [ -z "$CC" ]; then
    if [ -x "$HOME/.local/bin/llvm-mingw/bin/x86_64-w64-mingw32-clang" ]; then
        PATH="$HOME/.local/bin/llvm-mingw/bin:$PATH"
        export PATH
    fi
    if [ -d "$HOME/.local/bin" ]; then
        PATH="$HOME/.local/bin:$PATH"
        export PATH
    fi
    CC=x86_64-w64-mingw32-clang
fi
export CC

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "build.sh: cross compiler '$CC' not found." >&2
    echo "         Install llvm-mingw (for example into ~/.local/bin) or set CC." >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "build.sh: python3 is required (it generates the build files)." >&2
    exit 1
fi

if [ ! -d "$src" ]; then
    echo "build.sh: source directory '$src' not found." >&2
    exit 1
fi

if [ -n "$prefix" ]; then
    ZSH_WIN32_PREFIX="$prefix"
    export ZSH_WIN32_PREFIX
fi

if [ "$clean" = yes ]; then
    echo "==> removing $build"
    rm -rf "$build"
fi

mkdir -p "$build"

echo "==> building $build/zsh.exe"
cd "$src"
if [ -n "$jobs" ]; then
    make -j"$jobs" -f Src/win32/Makefile BUILDDIR="$build"
else
    make -f Src/win32/Makefile BUILDDIR="$build"
fi

if [ "$install" = yes ]; then
    echo "==> staging $build/install"
    make -f Src/win32/Makefile BUILDDIR="$build" install
fi

echo
echo "==> done"
ls -l "$build/zsh.exe"
if [ "$install" = yes ]; then
    echo "    staged tree: $build/install"
    echo "    copy its contents to C:\\Program Files\\zsh on the Windows machine"
fi
