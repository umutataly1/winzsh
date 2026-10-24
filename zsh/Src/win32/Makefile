# Makefile for the native Windows build of zsh.
#
# Usage (from the top of the zsh source tree):
#
#     make -f Src/win32/Makefile
#
# A mingw-w64 style cross compiler has to be in PATH, e.g.
#
#     export PATH=/opt/llvm-mingw/bin:$PATH
#
# The build is completely self contained: it does not use autoconf, and it
# does not use any POSIX emulation layer (no Cygwin, no MSYS); everything
# that zsh needs from a Unix system is implemented on top of the Win32 API
# in Src/win32/.

TOP      := $(CURDIR)
SRCDIR   := $(TOP)
BUILDDIR ?= $(TOP)/build-win32

CC       ?= x86_64-w64-mingw32-clang
STRIP    ?= x86_64-w64-mingw32-strip
PYTHON   ?= python3
WINDRES  ?= x86_64-w64-mingw32-windres

ifeq ($(origin WINDRES),default)
WINDRES := x86_64-w64-mingw32-windres
endif

# make has a built-in default for CC, so "?=" is not enough to pick the
# cross compiler: override it unless the user asked for something else.
ifeq ($(origin CC),default)
CC := x86_64-w64-mingw32-clang
endif
ifeq ($(origin STRIP),default)
STRIP := x86_64-w64-mingw32-strip
endif

# Windows version targeted (Windows 7 and later).
WINVER   := -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DNTDDI_VERSION=0x06010000

CPPFLAGS := -I$(BUILDDIR) \
            -I$(SRCDIR)/Src/win32/include \
            -I$(SRCDIR)/Src \
            $(WINVER)

CFLAGS   := -O2 -g0 -std=gnu11 -fno-strict-aliasing -fwrapv \
            -Wno-unused-parameter -Wno-unused-function \
            -Wno-missing-field-initializers -Wno-format \
            -Wno-macro-redefined

DEPFLAGS := -MMD -MP

LDFLAGS  := -static -static-libgcc
LIBS     := -ladvapi32 -lshell32 -luser32 -lws2_32 -lbcrypt

ALL_CPPFLAGS = $(CPPFLAGS) $(EXTRA_CPPFLAGS)

.PHONY: all clean install gen

all:
	@$(PYTHON) $(SRCDIR)/Src/win32/gen.py $(SRCDIR) $(BUILDDIR)
	@$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) \
	    $(BUILDDIR)/zsh.exe

gen:
	@$(PYTHON) $(SRCDIR)/Src/win32/gen.py $(SRCDIR) $(BUILDDIR)

-include $(BUILDDIR)/objects.mk

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILDDIR)/zsh.exe: $(OBJS) $(BUILDDIR)/zsh_manifest.o
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(BUILDDIR)/zsh_manifest.o $(LIBS)

$(BUILDDIR)/zsh_manifest.o: $(SRCDIR)/Src/win32/zsh.manifest.rc \
			    $(SRCDIR)/Src/win32/zsh.manifest
	$(WINDRES) $(SRCDIR)/Src/win32/zsh.manifest.rc -O coff -o $@

-include $(OBJS:.o=.d)

clean:
	rm -rf $(BUILDDIR)

# Stage a self-contained installation tree.  The layout matches the paths
# compiled in by gen.py (ZSH_WIN32_PREFIX, by default C:\Program Files\zsh),
# so copy the contents of INSTALLDIR to that prefix on the Windows machine.
INSTALLDIR ?= $(BUILDDIR)/install

install: all
	@rm -rf $(INSTALLDIR)
	@mkdir -p $(INSTALLDIR)/bin $(INSTALLDIR)/share/zsh/site-functions
	@cp $(BUILDDIR)/zsh.exe $(INSTALLDIR)/bin/zsh.exe
	@if [ -d $(SRCDIR)/Functions ]; then \
	    cp -R $(SRCDIR)/Functions $(INSTALLDIR)/share/zsh/functions; \
	fi
	@if [ -d $(SRCDIR)/Completion ]; then \
	    cdir=$(INSTALLDIR)/share/zsh/functions/Completion; \
	    mkdir -p $$cdir; \
	    find $(SRCDIR)/Completion -maxdepth 1 -type f -exec cp {} $$cdir/ \; ; \
	    for d in $(SRCDIR)/Completion/*/; do \
	        n=`basename $$d`; \
	        mkdir -p $$cdir/$$n; \
	        find $$d -maxdepth 2 -type f -exec cp {} $$cdir/$$n/ \; ; \
	    done; \
	fi
	@echo "Staged installation in $(INSTALLDIR)"
