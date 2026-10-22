/*
 * sys/ioctl.h - minimal ioctl interface for the native Win32 port
 */
#ifndef _WIN32_ZSH_SYS_IOCTL_H
#define _WIN32_ZSH_SYS_IOCTL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIOCGWINSZ	0x40087468
#define TIOCSWINSZ	0x80087467
#define TIOCGFLW	0x80087449
#define FIONREAD	0x541B
#define TIOCGPGRP	0x540F
#define TIOCSPGRP	0x5410
#define TIOCSETD	0x5411
#define TIOCNXCL	0x5412

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, unsigned long request, void *argp);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_SYS_IOCTL_H */
