#include <syscall.h>
#include <bits/stat.h>

#ifndef __NR_fstatat
#define __NR_fstatat __NR_newfstatat
#endif

inline static long sysfstatat(int dirfd, const char *path,
		struct stat *buf, int flags)
{
	return syscall4(__NR_fstatat, dirfd, (long)path, (long)buf, flags);
}
