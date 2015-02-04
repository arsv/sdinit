/* runcap is a companion utility to sninit, in the sense it does what
   could have been implemented in sninit (but wasn't).
   See doc/limits.txt for discussion of the problem.

   Yet even with background like this, runcap is completely independent from
   sninit. It could easily be packaged and distributed as a stanalone utility.

   This perfect separation of functions was actually one of the main reasons
   to make it standalone, vs. having all this built into init. */

/* The original name for this tool was "runcap", and it could set capabilities
   among other things. However, it turned out capabilities are badly broken
   in Linux at least as far as impose-restrictions-upon-child approach is
   considered, so all support for them was dropped and the tool was renamed
   to just "run". */

#define _GNU_SOURCE
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/fsuid.h>
#include <string.h>
#include <errno.h>

#include "run.h"

/* "Use errno value" indicator for die() */
#define ERRNO ((void*)-1)

/* For bits below */
#define SETSID   (1<<0)
#define SETCTTY  (1<<1)
#define REDIROUT (1<<2)
#define REDIRERR (1<<3)
#define NULLOUT  (1<<4)

/* Stored process attributes: uid/gids, fds, and session stuff */
/* Cgroups and ulimits are applied immediately, no need to store those */
int uid = -1;
int gid = -1;
int fsuid = -1;
int fsgid = -1;
/* Secondary groups */
int gidn = 0;
gid_t gids[MAXGROUPS];
/* Output redirection */
char* out = NULL;
/* Misc stuff to do (see constants above) */
int bits = 0;

/* The original argv[0] */
char* name;

/* Files for looking up uids and gids by name */
struct rcfile {
	const char* name;
	char* buf;
	size_t len;
	char* ls;		/* line start */
	char* le;		/* line end */
} passwd = {
	.name = "/etc/passwd"
}, groups = {
	.name = "/etc/group"
};

static void parseopt(char* opt);
static void parselim(char* opt);
static void addgroup(char* p);
static void setlimit(int key, char* p);
static void setcg(char* p);
static void setprio(char* p);
static void setsess(void);
static void setctty(void);
static void apply(char* cmd);

static uid_t finduser(char* p, int* logingroup);
static gid_t findgroup(char* p);

#define noreturn __attribute__((noreturn))
static void die(const char* msg, const char* arg, const char* err) noreturn;

int main(int argc, char** argv)
{
	char* argp;

	name = *(argv++);

	while(*argv) {
		argp = *argv;

		if(*argp != '-' && *argp != '+')
			break;
		else
			argv++;

		if(*argp == '+')
			parselim(argp + 1);
		else if(argp[1] == '-' && !argp[2])
			break;
		else
			parseopt(argp + 1);

	} if(!*argv)
		die("Usage: run [options] command arg arg ...", NULL, NULL);
	
	apply(*argv);
	execvp(*argv, argv);
	die("Can't exec ", *argv, ERRNO);
}

static void parseopt(char* opt)
{
	char c;
again:	switch(c = *(opt++)) {
		case '\0': break;

		case 'g': addgroup(opt);		break;
		case 'u':   uid = finduser(opt, &gid);	break;
		case 'F': fsuid = finduser(opt, &fsgid);break;
		case 'G': fsgid = findgroup(opt);	break;
		case 'C': setcg(opt);			break;

		case 'O': bits |= REDIRERR;
		case 'o': bits |= REDIROUT;
			  out = opt; break;

		case 'N': bits |= NULLOUT; goto again;
		case 'S': setsess(); goto again;
		case 'Y': setctty(); goto again;

		case 'r': setprio(opt); break;

		default: *opt = '\0';
			 die("Unsupported option -", (opt-1), NULL);
	}
}

/* ulimits take a lot of letter space, *and* need somewhat
   different treatment anyway. So -c is left for anything
   that's not ulimit, while ulimits are specified with +c */
static void parselim(char* opt)
{
	char c;
	int key;

	/* the keys follow bash(1) ulimit command */
	switch(c = *(opt++)) {
		case 'a': key = RLIMIT_AS;        break;
		   /* b is not implemented */
		case 'c': key = RLIMIT_CORE;      break;
		case 'd': key = RLIMIT_DATA;      break;
		case 'e': key = RLIMIT_NICE;      break;
		case 'f': key = RLIMIT_FSIZE;     break;
		case 'i': key = RLIMIT_SIGPENDING;break;
		case 'l': key = RLIMIT_MEMLOCK;   break;
		case 'm': key = RLIMIT_RSS;       break;
		case 'n': key = RLIMIT_NOFILE;    break;
		case 'q': key = RLIMIT_MSGQUEUE;  break;
		case 'r': key = RLIMIT_RTPRIO;    break;
		case 's': key = RLIMIT_STACK;     break;
		case 't': key = RLIMIT_CPU;       break;
		   /* u is not implemented */
		   /* v is not implemented */
		case 'x': key = RLIMIT_LOCKS;     break;
		case 'T': key = RLIMIT_NPROC;     break;
#ifdef RLIMIT_RTTIME
		/* musl lacks this? */
		case 'R': key = RLIMIT_RTTIME;    break;
#endif
		default: *opt = '\0';
			 die("Unrecognized ulimit key +", (opt-1), NULL);
	}

	setlimit(key, opt);
}

/* rc* to avoid name clashes with init_conf_mem, keeping ctags consistent */
/* There's no munmap; mmaped files are dropped during exec, so why bother */
static void rcmapfile(struct rcfile* f)
{
	int fd;
	struct stat st;

	if((fd = open(f->name, O_RDONLY)) < 0)
		die("Can't open ", f->name, ERRNO);
	if(fstat(fd, &st))
		die("Can't stat ", f->name, ERRNO);
	if(st.st_size > MAXFILELEN)
		die("Can't mmap ", f->name, "the file is too large");
	if((f->buf = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
		die("Can't mmap ", f->name, ERRNO);

	close(fd);
	f->len = st.st_size;

	f->ls = NULL;
	f->le = NULL;
};

static void rcrewind(struct rcfile* f)
{
	f->ls = NULL;
	f->le = NULL;
};

static char* rcnextline(struct rcfile* f)
{
	if(f->le)
		*(f->le) = '\n';
	f->ls = (f->le ? f->le + 1 : f->buf);
	if(f->ls >= f->buf + f->len)
		return NULL;
	
	for(f->le = f->ls; *(f->le) && *(f->le) != '\n'; f->le++);
	*(f->le) = '\0';

	return f->ls;
};

static int isnumeric(char* s)
{
	char* p;
	for(p = s; *p; p++)
		if(*p < '0' || *p > '9')
			return 0;
	return 1;
}

static uid_t finduser(char* user, int* gidp)
{
	char* l;
	char* name;
	char* uidstr;
	char* gidstr;

	if(isnumeric(user))
		return atoi(user);

	rcmapfile(&passwd);
	while((l = rcnextline(&passwd))) {
		name = strsep(&l, ":");
		if(strcmp(name, user))
			continue;
		strsep(&l, ":");
		uidstr = strsep(&l, ":");
		gidstr = strsep(&l, ":");
		if(!uidstr || !gidstr)
			continue;
		if(gidp && *gidp < 0)
			*gidp = atoi(gidstr);
		return atoi(uidstr);
	}

	die("Unknown user ", user, NULL);
};

static gid_t findgroup(char* group)
{
	char* l;
	char* grpname;
	char* gidstr;

	if(isnumeric(group))
		return atoi(group);

	if(!groups.buf)
		rcmapfile(&groups);
	else
		rcrewind(&groups);

	while((l = rcnextline(&groups))) {
		grpname = strsep(&l, ":");
		if(strcmp(grpname, group))
			continue;
		strsep(&l, ":");
		gidstr = strsep(&l, ":");
		return atoi(gidstr);
	};

	die("Unknown group ", group, NULL);
}

static void addgroup(char* group)
{
	gid_t g = findgroup(group);

	if(gid < 0)
		gid = g;
	if(gidn >= MAXGROUPS)
		die("Too many groups", NULL, NULL);
	gids[gidn++] = g;
};

/* Limits could have been stored just like uid/gids... except it's not needed.
   Unlike uid/gid changes, limit settings are independent and do not prevent
   run from doing its work even if set immediately. So, why bother. */
static void setlimit(int key, char* lim)
{
	char* hard;
	char* soft;
	struct rlimit rl;

	char* q = lim;
	soft = strsep(&q, "/");
	hard = q;

	rl.rlim_cur = atoi(soft);
	rl.rlim_max = hard ? atoi(hard) : rl.rlim_cur;

	if(setrlimit(key, &rl))
		die("Can't set ulimit ", lim, ERRNO);
};

/* may be used by strerror, so exported */
char* ltoa(long num)
{
	static char buf[16];
	int i = sizeof(buf)-1;
	buf[i--] = '\0';
	while(num && i >= 0) {
		buf[i--] = num % 10;
		num /= 10;
	};
	return buf + i + 1;
}

static void setcg(char* cg)
{
	int cgl = strlen(CGBASE) + strlen(cg) + 15;
	char buf[cgl];
	int fd;
	char* pid;
	char* cgp;

	if(strchr(cg, '/')) {
		cgp = cg;
	} else {
		strcpy(buf, CGBASE);
		strcat(buf, "/");
		strcat(buf, cg);
		strcat(buf, "/tasks");
		cgp = buf;
	}

	if((fd = open(cgp, O_WRONLY)) < 0)
		die("Can't open ", cgp, ERRNO);

	pid = ltoa(getpid());
	if(write(fd, pid, strlen(pid)) <= 0)
		die("Can't add process to ", cgp, ERRNO);
	close(fd);
}

/* Linux uses 1..40 range for process priorities and a return of -1
   from getpriority means error. Now of course glibc people can not
   just stick with the native kernel data so they wrap to (apparently
   POSIX-standard) range of -19..20 */
#ifdef __GLIBC__
#include <sys/syscall.h>
#define setpriority(a, b, c) syscall(__NR_setpriority, a, b, c)
#define getpriority(a, b)    syscall(__NR_getpriority, a, b)
#endif

static void setprio(char* p)
{
	if(setpriority(PRIO_PROCESS, 0, atoi(p)))
		die("setpriority failed", NULL, ERRNO);
}

static void setsess(void)
{
	if(setsid() < 0)
		die("setsid failed", NULL, ERRNO);
}

static void setctty(void)
{
	if(ioctl(1, TIOCSCTTY, 0) < 0)
		die("ioctl(TIOCSCTTY) failed", NULL, ERRNO);
}

static int checkopen(const char* name)
{
	int fd = open(name, O_RDWR);

	if(fd < 0)
		die("Can't open ", name, ERRNO);
	else
		return fd;
}

static int openlog(const char* name)
{
	if(!strchr(name, '/'))
		return checkopen(name);

	int namelen = strlen(name);
	char* base = LOGDIR;
	int baselen = strlen(base);
	int outlen = baselen + namelen + 1;
	char outbuf[outlen + 2];

	memcpy(outbuf, base, baselen);
	outbuf[baselen] = '/';
	memcpy(outbuf + baselen + 1, name, namelen);
	outbuf[outlen] = '\0';

	return checkopen(outbuf);
}

/* const char* actually but alas this is how it is declared in <string.h> */
char* basename(const char* path)	
{
	const char* p = path;
	const char* q = path;

	while(*p) if(*(p++) == '/') q = p;

	return (char*)q;
}

static void apply(char* cmd)
{
	int outfd;

	if(fsuid >= 0)
		if(setfsuid(fsuid))
			die("setfsuid failed", NULL, ERRNO);
	if(fsgid >= 0)
		if(setfsgid(fsgid))
			die("setfsgid failed", NULL, ERRNO);

	if(gid >= 0)
		if(setresgid(gid, gid, gid))
			die("setresgid failed", NULL, ERRNO);
	if(uid >= 0)
		if(setresuid(uid, uid, uid))
			die("setresuid failed", NULL, ERRNO);

	if(bits & (NULLOUT)) {
		outfd = checkopen("/dev/null");
		dup2(outfd, 0);
		if(!(bits & REDIROUT))
			dup2(outfd, 1);
		if(!(bits & REDIRERR))
			dup2(outfd, 2);
		if(outfd > 2)
			close(outfd);
	}

	if(bits & (REDIROUT | REDIRERR)) {
		outfd = openlog(out && *out ? out : basename(cmd));
		if(bits & REDIROUT)
			dup2(outfd, 1);
		if(bits & REDIRERR)
			dup2(outfd, 2);
		if(outfd > 2)
			close(outfd);
	}
}

static char* strlncat(char* buf, char* ptr, int buflen, const char* str)
{
	int len = strlen(str);
	int avail = (buf + buflen) - ptr;
	if(len > avail) len = avail;
	memcpy(ptr, str, len);
	return ptr + len;
}

static void die(const char* msg, const char* arg, const char* err)
{
	int buflen = 1024;
	char buf[buflen+1];
	char* ptr = buf;

	ptr = strlncat(buf, ptr, buflen, msg);
	if(arg)
		ptr = strlncat(buf, ptr, buflen, arg);
	if(err == ERRNO)
		err = strerror(errno);
	if(err) {
		ptr = strlncat(buf, ptr, buflen, ": ");
		ptr = strlncat(buf, ptr, buflen, err);
	}
	*ptr = '\n';

	write(2, buf, ptr - buf + 1);
	_exit(-1);
}
