/*
 * sys/types.h - Win32 extension of the Microsoft C runtime's sys/types.h
 *
 * The Microsoft C runtime knows about pid_t, mode_t, off_t, dev_t and
 * ino_t, but not about the user and group identifiers that zsh uses in
 * a couple of places.  This header pulls in the CRT header and adds the
 * missing types.
 */

#ifndef _WIN32_ZSH_SYS_TYPES_H
#define _WIN32_ZSH_SYS_TYPES_H

/*
 * The Microsoft C runtime uses a 64 bit pid_t on 64 bit systems, but
 * its getpid() returns an int.  Windows process ids are 32 bit values,
 * so use an int here as well; this keeps the two consistent.  The
 * guard of the runtime header is claimed so that it does not define
 * pid_t a second time.
 */
#ifndef _PID_T_
#define _PID_T_
typedef int _pid_t;
typedef int pid_t;
#endif

#include_next <sys/types.h>

#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
#endif

#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef unsigned int gid_t;
#endif

#endif /* _WIN32_ZSH_SYS_TYPES_H */
