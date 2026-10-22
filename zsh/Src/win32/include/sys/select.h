/*
 * sys/select.h - select() for the native Win32 port
 */
#ifndef _WIN32_ZSH_SYS_SELECT_H
#define _WIN32_ZSH_SYS_SELECT_H

#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

typedef struct fd_set {
    unsigned int fds_bits[(FD_SETSIZE + 31) / 32];
} fd_set;

#define FD_ZERO(s) memset((s), 0, sizeof(fd_set))
#define FD_SET(fd, s) \
    (((fd) >= 0 && (fd) < FD_SETSIZE) ? \
     ((s)->fds_bits[(fd) / 32] |= (1u << ((fd) % 32)), 0) : 0)
#define FD_CLR(fd, s) \
    (((fd) >= 0 && (fd) < FD_SETSIZE) ? \
     ((s)->fds_bits[(fd) / 32] &= ~(1u << ((fd) % 32)), 0) : 0)
#define FD_ISSET(fd, s) \
    (((fd) >= 0 && (fd) < FD_SETSIZE) ? \
     ((s)->fds_bits[(fd) / 32] & (1u << ((fd) % 32))) : 0)

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	   struct timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_SYS_SELECT_H */
