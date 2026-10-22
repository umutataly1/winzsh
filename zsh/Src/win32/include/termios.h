/*
 * termios.h - POSIX-like terminal control for the native Win32 port
 *
 * The structures and constants mirror the traditional Unix interface so
 * that the zsh sources need no changes; the implementation in
 * Src/win32/win32_term.c maps them onto the Win32 console API.
 */

#ifndef _WIN32_ZSH_TERMIOS_H
#define _WIN32_ZSH_TERMIOS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The values are the traditional Unix ones; they are written in
 * hexadecimal because the octal notation of some of them is not valid
 * C.
 */

/* c_iflag */
#define IGNBRK	0x0001
#define BRKINT	0x0002
#define IGNPAR	0x0004
#define PARMRK	0x0008
#define INPCK	0x0010
#define ISTRIP	0x0020
#define INLCR	0x0040
#define IGNCR	0x0080
#define ICRNL	0x0100
#define IUCLC	0x0200
#define IXON	0x0400
#define IXANY	0x0800
#define IXOFF	0x1000
#define IMAXBEL	0x2000
#define IUTF8	0x4000

/* c_oflag */
#define OPOST	0x0001
#define ONLCR	0x0004
#define OCRNL	0x0008
#define ONOCR	0x0020
#define OFDEL	0x0040
#define NLDLY	0x0080
#define CRDLY	0x0200
#define TABDLY	0x1200
#define BSDLY	0x1000
#define FFDLY	0x2000
#define VTDLY	0x8000
#define OFLOW	0x10000

/* c_cflag */
#define CSIZE	0x0030
#define CS5	0x0000
#define CS6	0x0020
#define CS7	0x0030
#define CS8	0x0030
#define CSTOPB	0x0040
#define CREAD	0x0080
#define PARENB	0x0100
#define PARODD	0x0040
#define HUPCL	0x0100
#define CLOCAL	0x0800
#define CRTSCTS	0x200000000
#define CBAUD	0x52002000
#define B9600	0x000A
#define B19200	0x000C
#define B38400	0x000E
#define B57600	0x0008
#define B115200	0x0009
#define B230400	0x0019
#define B460800	0x001A
#define B921600	0x001B

/* c_lflag */
#define ISIG	0x0001
#define ICANON	0x0002
#define XCASE	0x0004
#define ECHO	0x0008
#define ECHOE	0x0010
#define ECHOK	0x0020
#define ECHONL	0x0040
#define NOFLSH	0x0040
#define TOSTOP	0x0100
#define ECHOCTL	0x0200
#define ECHOPRT	0x0400
#define ECHOKE	0x0800
#define FLUSHO	0x1000
#define PENDIN	0x0800
#define IEXTEN	0x8000

/* c_cc indexes */
#define VINTR	0
#define VQUIT	1
#define VERASE	2
#define VKILL	3
#define VEOF	4
#define VTIME	5
#define VMIN	6
#define VSWTCH	7
#define VSTART	8
#define VSTOP	9
#define VSUSP	10
#define VEOL	11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT	15
#define VEOL2	16
#define VSTATUS 17
#define NCCS	18

#define CSWTCH	0xFFFF

/* tcsetattr() options */
#define TCSANOW	0
#define TCSADRAIN	1
#define TCSAFLUSH	2

#define TCSAFLUSH_COMPAT	TCSAFLUSH

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

typedef struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
} termios_t;

typedef struct termios termios;

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgid);
int tcdrain(int fd);
int tcflush(int fd, int queue_selector);
pid_t tcgetpid(int fd);

speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);

void cfmakeraw(struct termios *termios_p);

#define TCSAFLUSH_  TCSAFLUSH

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_TERMIOS_H */
