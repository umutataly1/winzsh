/*
 * sys/resource.h - resource usage for the native Win32 port
 */
#ifndef _WIN32_ZSH_SYS_RESOURCE_H
#define _WIN32_ZSH_SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The resource numbers follow the Linux convention so that the shell's
 * resource names line up; Windows has no resource limits in the Unix
 * sense, so only RLIMIT_NOFILE reports a meaningful value.
 */
#define RLIMIT_CPU	0
#define RLIMIT_FSIZE	1
#define RLIMIT_DATA	2
#define RLIMIT_STACK	3
#define RLIMIT_CORE	4
#define RLIMIT_RSS	5
#define RLIMIT_NPROC	6
#define RLIMIT_NOFILE	7
#define RLIMIT_MEMLOCK	8
#define RLIMIT_AS	9
#define RLIMIT_LOCKS	10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE	12
#define RLIMIT_NICE	13
#define RLIMIT_RTPRIO	14
#define RLIMIT_RTTIME	15
#define RLIMIT_NLIMITS	16
#define RLIM_NLIMITS	16

/*
 * Windows has no resource limits in the Unix sense, but the shell still
 * wants the types to exist.  A limit is a 64 bit quantity so that
 * RLIM_INFINITY can be represented.
 */
typedef unsigned long long rlim_t;

#define RLIM_INFINITY	0xffffffffffffffffULL

#define RUSAGE_SELF	0
#define RUSAGE_CHILDREN	1

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
int getrusage(int who, struct rusage *usage);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32_ZSH_SYS_RESOURCE_H */
