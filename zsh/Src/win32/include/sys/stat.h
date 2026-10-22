/*
 * sys/stat.h - Win32 extension of the Microsoft C runtime's sys/stat.h
 *
 * The Microsoft C runtime knows only the one argument version of mkdir()
 * that is declared by <io.h> and <direct.h>.  zsh uses the POSIX
 * interface, which also takes the mode of the new directory, so the calls
 * are redirected to the implementation in Src/win32/compat.c.
 */

#ifndef _WIN32_ZSH_SYS_STAT_H
#define _WIN32_ZSH_SYS_STAT_H

#include_next <sys/stat.h>

#include <sys/types.h>

/*
 * <io.h> and <direct.h> declare the one argument version of mkdir();
 * make sure that they have been read before the macro below is installed,
 * otherwise their declarations would be mangled by it.
 */
#ifndef _IO_H_
#include <io.h>
#endif
#ifndef _INC_DIRECT
#include <direct.h>
#endif

int zsh_win32_mkdir(const char *path, mode_t mode);

#undef mkdir
#define mkdir(path, mode) zsh_win32_mkdir((path), (mode))

/*
 * The mode bits the Microsoft C runtime reports do not reflect the
 * Windows access model: every directory comes out as 0777 and every
 * file as 0666, whatever the actual permissions are.  Shell scripts do
 * look at these bits (the completion system refuses to run when it sees
 * world-writable directories), so stat() and fstat() are redirected to
 * the implementations in Src/win32/compat.c, which report a more
 * plausible set of bits.
 */
int zsh_win32_stat(const char *path, struct stat *buf);
int zsh_win32_fstat(int fd, struct stat *buf);

#undef stat
#define stat(path, buf) zsh_win32_stat((path), (buf))
#undef fstat
#define fstat(fd, buf) zsh_win32_fstat((fd), (buf))

#endif /* _WIN32_ZSH_SYS_STAT_H */
