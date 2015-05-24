#define _GNU_SOURCE
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "config.h"
#include "sys.h"
#include "init.h"
#include "scope.h"

/* What init does is essentially running a set of processes.
   The set currently configured is stored in cfg.
   Putting it another way, cfg is inittab and/or initdir data compiled
   into init's internal format.
   Volatile per-process data (pids, timestamps) is also here.

   The struct is declared weak to allow linking build-in inittab over. */

weak struct config* cfg;

/* Sninit uses the notion of runlevels to tell which entries from inittab
   should be running at any given moment and which should not.

   At any given time, init "is in" a single primary runlevel, possibly
   augmented with any number of sublevels. The whole thing is stored as
   a bitmask: runlevel 3ab is (1<<3) | (1<<a) | (1<<b).

   Switching between runlevels is initiated by setting nextlevel to something
   other than currlevel. The switch is completed once currlevel = nextlevel.

   Check shouldberunning() on how entries are matched against current runlevel,
   and initpass() for level-switching code.

   Init starts at runlevel 0, so 0~ entries are not spawned during boot.
   When shutting down, we first switch back to level 0 = (1<<0) and then
   to "no-level" which is value 0, making sure all entries get killed. */

int currlevel = (1 << 0);
int nextlevel = INITDEFAULT;

/* Normally init sleeps in ppoll until dusturbed by a signal or a socket
   activity. However, initpass may want to set an alarm, so that it would
   send SIGKILL 5 seconds after SIGTERM if the process refuses to die.
   This is done by setting timetowait, which is later used for ppoll timeout.

   Default value here is -1, which means "sleep indefinitely".
   main sets that before each initpass() */

int timetowait;

/* To set timestamps on initrecs (lastrun, lastsig), initpass must have
   some kind of current time value available. Instead of making a syscall
   for every initrec that needs it, the call is only made before initpass
   and the same value is then used during the pass. See also setpasstime() */

time_t passtime;

/* sninit shuts down the system by calling reboot(rbcode) after exiting
   the main loop. In other words, reboot command internally is just
       nextlevel = (1<<0);
       rbcode = RB_AUTOBOOT;
   The values are described in <sys/reboot.h> (or check reboot(2)) */

int rbcode = RB_HALT_SYSTEM;

/* These fds are kept open more or less all the time.
   initctl is the listening socket, syslogfd is /dev/log, warnfd is
   where warn() will put its messages. */

int initctlfd;
int warnfd = 0;		/* stderr only, see warn() */
int syslogfd = -1;	/* not yet opened */

/* Init blocks all most signals when not in ppoll. This is the orignal
   pre-block signal mask, used for ppoll and passed to spawned children. */

sigset_t defsigset;

/* S_* flags to signal we have a pending telinit connection or
   a reconfiguration request */

int state = 0;

/* Short outline of the code: */

export int main(int argc, char** argv);		/* main loop */

local int setup(int argc, char** argv);		/* initialization */
local int setinitctl(void);
local void setsignals(void);
local void setargs(int argc, char** argv);
local int setpasstime(void);

extern int configure(int);		/* inittab parsing */
extern void setnewconf(void);

extern void initpass(void);		/* the core: process (re)spawning */
extern void waitpids(void);

extern void pollctl(void);		/* telinit communication */
extern void acceptctl(void);

local void sighandler(int sig);		/* global singnal handler */
local void forkreboot(void);		/* reboot(rbcode) */


/* main(), the entry point also the main loop.

   Overall logic here: interate over inittab records (that's initpass()),
   go sleep in ppoll(), iterate, sleep in ppoll, iterate, sleep in ppoll, ...

   Within this cycle, ppoll is the only place where blocking occurs.
   Even when running a wait entry, init does not use blocking waitpid().
   Instead, it spawns the process and goes to sleep in ppoll until
   the process dies.

   Whenever there's a need to disturb the cycle, flags are raised in $state.
   Any branching to handle particular situation, like child dying or telinit
   knocking on the socket, occurs here in main.

   For time-tracking code, see longish comment near setpasstime() below.
   Time is only checked once for each initpass (that's why "passtime").

   Barring early hard errors, the only way to exit the main loop is switching
   to "no-runlevel" state, which is (currlevel == 0). Note this is different
   from runlevel 0 which is (1<<0). See the block at the end of initpass. */

int main(int argc, char** argv)
{
	if(setup(argc, argv))
		goto reboot;	/* Initial setup failed badly */
	if(setpasstime())
		passtime = BOOTCLOCKOFFSET;

	while(1)
	{
		warnfd = 2;
		timetowait = -1;

		initpass();		/* spawn/kill processes */

		if(!currlevel)
			goto reboot;

		if(!(state & S_WAITING) && (state & S_RECONFIG))
			setnewconf();

		pollctl();		/* waiting happens here */

		if(setpasstime() && timetowait > 0)
			passtime += timetowait;

		if(state & S_SIGCHLD)
			waitpids();	/* reap dead children */

		if(state & S_INITCTL)
			acceptctl();	/* telinit communication */
	}

reboot:
	warnfd = 0;		/* stderr only, do not try syslog */
	if(!(state & S_PID1))	/* we're not running as *the* init */
		return 0;

	forkreboot();
	return 0xFE; /* feh */
};

int setup(int argc, char** argv)
{
	/* To avoid calling getpid every time. And since this happens to be
	   the first syscall init makes, it is also used to check whether
	   runtime situation is bearable. */
	if(getpid() == 1)
		state |= S_PID1;
	/* Failing getpid() is a sign of big big trouble, like running x86
	   or x32 on a x64 kernel without relevant parts built in.
	   If it is the case, error reporting is pointless and all init can do
	   is bail out asap. */
	else if(errno)
		_exit(errno);

	if(setinitctl())
		/* Not having telinit is bad, but aborting system startup
		   for this mere reason is likely even worse. */
		warn("can't initialize initctl, init will be uncontrollable");

	setsignals();
	setargs(argc, argv);

	if(!configure(NONSTRICT))
		setnewconf();
	else if(!cfg)
		retwarn(-1, "initial configuration error");

	return 0;
}

/* init gets any part of kernel command line the kernel itself could not parse.
   Among those, the only thing that concerns init is possible initial runlevel
   indication, either a (single-digit) number or a word "single".
   init does not pass its argv to any of the children. */
void setargs(int argc, char** argv)
{
	char** argi;

	for(argi = argv; argi - argv < argc; argi++)
		if(!strcmp(*argi, "single"))
			nextlevel = (1 << 1);
		else if(**argi >= '1' && **argi <= '9' && !*(*argi+1))
			nextlevel = (1 << (**argi - '0'));
}

/* Outside of ppoll, we only block SIGCHLD; inside ppoll, default sigmask
   is used. This should be ok since linux blocks signals to init from other
   processes, and blocking kernel-generated signals rarely makes sense.
   Normally init shouldn't be getting them, aside from SIGCHLD and maybe
   SIGPIPE/SIGALARM during telinit communication. If anything else is sent
   (SIGSEGV?), then we're already well out of normal operation range
   and should accept whatever the default action is. */
void setsignals(void)
{
	/* Restarting read() etc is ok, the calls init needs interrupted
	   will be interrupted anyway.
	   Exception: SIGALRM must be able to interrput write(), telinit
	   timeout handling is built around this fact. */
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};

	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sa.sa_mask, &defsigset);

	/* These should have been signal(2) calls, but since signal(2) tells us
	   to "avoid its use", we'll call sigaction instead.
	   After all, BSD-compatible signal() implementations (which is to say,
	   pretty much all of them) are just wrappers around sigaction(2). */

	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);

	sa.sa_flags = SA_NOCLDSTOP; 	/* init does not care about children */
	sigaction(SIGCHLD, &sa, NULL);	/*   being stopped */
	
	/* These should interrupt write() calls, and that's enough */
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
}

int setinitctl(void)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = INITCTL
	};

	/* This way readable "@initctl" can be used for reporting below,
	   and config.h looks better too. */
	if(addr.sun_path[0] == '@')
		addr.sun_path[0] = '\0';

	/* we're not going to block for connections, just accept whatever
	   is already there; so it's SOCK_NONBLOCK */
	if((initctlfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) < 0)
		retwarn(-1, "Can't create control socket: %m");

	if(bind(initctlfd, (struct sockaddr*)&addr, sizeof(addr))) 
		gotowarn(close, "Can't bind %s: %m", INITCTL)
	else if(listen(initctlfd, 1))
		gotowarn(close, "listen() failed: %m");

	return 0;

close:
	close(initctlfd);
	initctlfd = -1;
	return -1;
}

/* A single handler for all four signals we care about. */
void sighandler(int sig)
{
	switch(sig)
	{
		case SIGCHLD:
			state |= S_SIGCHLD;
			break;
			
		case SIGTERM:	/* C-c when testing */
		case SIGINT:	/* C-A-Del with S_PID1 */
			rbcode = RB_AUTOBOOT;
			nextlevel = (1<<0);
			break;

		case SIGHUP:
			if(initctlfd >= 0)
				close(initctlfd);
			setinitctl();
			break;
	}
}

/* Throughout the loop, main() keeps track of current time which initpass()
   then uses for things like timed SIGTERM/SIGKILL, fast respawns and so on.
   Via timetowait, the above affects ppoll timeout in pollfds(),
   making the main loop run a bit faster than it would with only SIGCHLDs
   and telinit socket noise.

   Precision is not important here, it is ok to wait for a few seconds more,
   but keeping sane timeouts is crucial; setting poll timeout to 0 by mistake
   would result in the loop spinning out of control. This is done by tracking
   non-zero setpasstime returns and "pushing" passtime forward, pretending
   that any timed ppoll did not return early.

   Note clock errors are not something that happens daily, and usually
   is's a sign of deep troubles, like running on an incompatible architecture.
   Actually rebooting on clock_gettime failure does not sound like a bad
   idea at all, and that's exactly what main() does when the very first
   clock_gettime call fails.

   Still, once we have the system running, it makes sense to try handling
   the situation gracefully. After all, timing stuff is somewhat auxillilary
   in a non-realtime unix, a matter of convenience, not correctness, and
   init could (should?) have been written with no reliance on time. */

/* The value used for passtime is kernel monotonic clock shifted
   by a constant. Monotonic clock works well, since the code only uses
   passtime differences, not the value itself. Constant shift is necessary
   to make sure the difference is not zero at the first initpass.

   At bootup, the system starts with all lastrun=0 and possibly also
   with the clock near 0, activating time_to_* timers even though
   no processes have been started at time 0. To avoid delays, monotonic
   clocks are shifted forward so that boot occurs at some time past 0.

   The offset may be as low as the actual value of time_to_restart,
   but since time_to_restart is a short using 0xFFFF is a viable option.
   After all, even with offset that large monotonic clock values are much
   much lower than those routinely returned by CLOCK_REALTIME. */

int setpasstime(void)
{
	struct timespec tp;

	if(clock_gettime(CLOCK_MONOTONIC, &tp))
		retwarn(-1, "clock failed: %m");

	passtime = tp.tv_sec + BOOTCLOCKOFFSET;

	return 0;
}

/* Linux kernel treats reboot() a lot like _exit(), including, quite
   surprisingly, panic when it's init who calls it. So we've got to fork
   here and call reboot for the child process. Now because vfork may
   in fact be fork, and scheduler may still be doing tricks, it's better
   to wait for the child before returning, because return here means
   _exit from init and immediate panic. */
void forkreboot(void)
{
	int pid;
	int status;

	if((pid = vfork()) == 0)
		_exit(reboot(rbcode));
	else if(pid < 0)
		return;
	if(waitpid(pid, &status, 0) < 0)
		return;
	if(!WIFEXITED(status) || WEXITSTATUS(status))
		warn("still here, reboot failed, time to panic");
}
