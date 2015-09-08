#include <string.h>
#include <signal.h>
#include <sys/reboot.h>
#include "init.h"
#include "scope.h"

extern int state;
extern int nextlevel;
extern int rbcode;

export void parsecmd(char* cmd);

extern int configure(int strict);
extern void stop(struct initrec* p);
extern struct initrec* findentry(const char* name);

extern void dumpstate(void);
extern void dumpidof(struct initrec* p);

local void setrunlevel(const char* cmd);
local void dorestart(struct initrec* p);
local void dodisable(struct initrec* p, int v);
local void dostart(struct initrec* p);
local void killrec(struct initrec* p, int sig);

local void clearts(struct initrec* p);

/* Here we have a single command (cmd) sent by telinit, stored in some
   buffer in readcmd(). And we've got to parse it and take action.
   The actual command is always cmd[0], while cmd[1:] is the (optional) argument.
   Examples:
	"c"		reconfigure
	"9"		switch to runlevel 9
	"shttpd"	start httpd

   With exception of kill() calls, all parsecmd does is setting flags,
   either per-process or global. The next initpass() is where the rest
   happens. Telinit connection is closed right after parsecmd() returns;
   this way initpass remains async and simple.

   Within parsecmd, warnfd is the open telinit connection, so we send
   the text back to telinit using warn(). */

void parsecmd(char* cmd)
{
	char* arg = cmd + 1;
	struct initrec* p = NULL;

	/* Check whether this command needs arguments */
	switch(*cmd) {
		/* Runlevel switching commands handle argument differently */
		case '0' ... '9':
		case '+':
		case '-':
			setrunlevel(cmd);
			return;

		/* Mandatory argument */
		case 'r':		/* restart */
		case 's': case 't':	/* start, stop */
		case 'u':		/* unstop */
		case 'p': case 'w':	/* pause, resume */
		case 'h': case 'i':	/* hup, pidof */
			if(!(p = findentry(arg)))
				retwarn_("can't find %s in inittab", arg);
			break;

		/* There are no commands with optional arguments atm */
		/* There are few that take no argument at all however */
		default:
			if(*arg)
				retwarn_("no argument allowed for %c", *cmd);
	}

	/* Now the command itself */
	switch(*cmd) {
		/* halt */
		case 'H': nextlevel = 1; rbcode = RB_HALT_SYSTEM; break;
		case 'P': nextlevel = 1; rbcode = RB_POWER_OFF;   break;
		case 'R': nextlevel = 1; rbcode = RB_AUTOBOOT;    break;
		/* process ops */
		case 'p': killrec(p, -SIGSTOP); break;
		case 'w': killrec(p, -SIGCONT); break;
		case 'h': killrec(p, SIGHUP); break;
		case 's': dostart(p); break;
		case 'r': dorestart(p); break;
		case 't': dodisable(p, 1); break;
		case 'u': dodisable(p, 0); break;
		/* state query */
		case '?': dumpstate(); break;
		case 'i': dumpidof(p); break;
		/* reconfigure */
		case 'c':
			if(configure(STRICT))
				warn("Reconfiguration failed");
			else
				state |= S_RECONFIG;
			break;
		default:
			warn("unknown command %s", cmd);
	}
}

/* Possible runlevel change commands here:
	4	switch to runlevel 4, leaving sublevels unchanged
	4-	switch to runlevel 4, clear sublevels
	4ab	switch to runlevel 4, clear sublevels, activate a and b
	+ab	activate sublevels a and b
	-ac	deactivate sublevels a and c */

void setrunlevel(const char* p)
{
	int next = nextlevel;
	int mask = 0;
	char op = *(p++);

	if(op >= '0' && op <= '9')
		next = (next & SUBMASK) | (1 << (op - '0'));
	/* should have checked for +/- here, but it's done in parsecmd before calling setrunlevel */

	if(*p == '-') {
		/* "4-" or similar */
		next &= ~SUBMASK;
		if(*(++p))
			retwarn_("no runlevels allowed past trailing -");
	} else for(; *p; p++)
		/* "4abc", or "+abc", or "-abc" */
		if(*p >= 'a' && *p <= 'f')
			mask |= (1 << (*p - 'a' + 0xa));
		else
			retwarn_("bad runlevel %c", *p);

	switch(op) {
		default: if(mask) next &= ~SUBMASK;
		case '+': next |=  mask; break;
		case '-': next &= ~mask; break;
	}

	nextlevel = next;
}

/* User commands like start or stop should prompt immediate action
   (well, next-initpass-immediate that is), disregarding any possible
   time_to_* timeouts. To achieve that, entry timestamps are reset. */

void clearts(struct initrec* p)
{
	p->lastrun = 0;
	p->lastsig = 0;
}

/* What this does is a forced start of a service,
   unlike enable which is more like "un-stop" and may leave
   the service stopped if it is not in current runlevel.

   The idea is to change runlevel mask so that the entry
   would be started in current runlevel, *and* let the
   service go down after a runlevel switch if it is
   configured to do so. Forcing always-on state with something
   like P_ENABLED may break sleep modes and expected shutdown
   routine.
 
   There is no dostop because P_MANUAL is enough to
   force-stop a process regardless of its configured runlevels */

void dostart(struct initrec* p)
{
	/* C_INVERT is never used with anything that can be started */
	if(p->flags & C_INVERT) return;
	clearts(p);
	p->rlvl |= (nextlevel & PRIMASK);
	p->rlvl &= (nextlevel & SUBMASK) | PRIMASK;
	p->flags &= ~(P_MANUAL | P_FAILED);
}

/* To restart a process, it's enough to kill it without touching
   the flags. On the next initpass, the process will be re-spawned.

   This won't work with non-respawning entries of cource, but it does
   not make much sense to restart those anyway. */

void dorestart(struct initrec* p)
{
	clearts(p);
	stop(p);
}

/* Enabling/disabling p may prompt some action on the next initpass,
   either spawn() or stop(), and we'd like to have that done immediately
   regardless of the timestamps. The action itself will happen more
   or less naturally once shouldberunning() flips over P_MANUAL. */

void dodisable(struct initrec* p, int v)
{
	clearts(p);
	p->flags &= ~P_FAILED;
	if(v)
		p->flags |= P_MANUAL;
	else
		p->flags &= ~P_MANUAL;
}

/* When signalling HUP, we only want to target the immediate init child.
   With SIGSTOP/SIGCONT however, targeting the whole group makes more sense
   (but may break programs that use SIGSTOP internally, hm)

   The state of the process after SIGSTOP/SIGCONT will be tracked in waitpids(). */

void killrec(struct initrec* p, int sig)
{
	pid_t pid = p->pid;

	if(pid <= 0)
		retwarn_("%s is not running", p->name);

	if(sig < 0) {
		pid = -pid;
		sig = -sig;
	}

	if(kill(pid, sig))
		retwarn_("%s[%i]: kill failed: %e", p->name, p->pid);
}
