/*
 * sys/utsname.h - uname() support for the native Win32 port
 */
#ifndef _WIN32_ZSH_SYS_UTSNAME_H
#define _WIN32_ZSH_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define UTSL 9

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

int uname(struct utsname *name);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_SYS_UTSNAME_H */
