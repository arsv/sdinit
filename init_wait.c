#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "init.h"
#include "scope.h"
#include "sys.h"

extern int state;
extern int nextlevel;
extern struct config* cfg;
extern time_t passtime;

export void waitpids(void);

local void markdead(struct initrec* p, int status);
local void checkfailacts(struct initrec* p, int failed);
local int badsignal(int sig);

/* So we were signalled SIGCHLD and got to check what's up with
   the children. And because we care about killing paused (as in SIGSTOP)
   children gracefully, we want to track running/stopped status as well.
   Linux allows this with WUNTRACED and WCONTINUED. */

void waitpids(void)
{
	pid_t pid;
	int status;
	struct initrec *p, **pp;
	const int flags = WNOHANG | WUNTRACED | WCONTINUED;

	while((pid = waitpid(-1, &status, flags)) > 0)
	{
		for(pp = cfg->inittab; (p = *pp); pp++)
			if(p->pid == pid)
				break;
		if(!p)	/* Some stray child died. Like we care. */
			continue;

		if(WIFSTOPPED(status))
			p->flags |= P_SIGSTOP;
		else if(WIFCONTINUED(status))
			p->flags &= ~P_SIGSTOP;
		else
			markdead(p, status);
	}

	state &= ~S_SIGCHLD;
}

/* Abnormal exits should be reported, and in case of DTF/DOF taken
   actions upon. It is not a simple 0-return check however: applying
   DOF/DTF to entries likely killed by the user is not a good idea,
   and entries killed by init should not be reported. */

void markdead(struct initrec* p, int status)
{
	if(WIFEXITED(status) && !WEXITSTATUS(status))
		;    /* zero exits are ok */
	else if(WIFSIGNALED(status) && (p->flags & (P_SIGTERM | P_SIGKILL)))
		;    /* expected exit, init killed it */
	else if(p->flags & C_HUSH)
		;    /* should not be reported */
	else warn("%s[%i] abnormal exit %i", p->name, p->pid,
		WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));

	if(p->flags & C_ONCE)
		;  /* no point in timing run-once entries */
	else if(p->flags & (C_DOF | C_DTF))
		checkfailacts(p, status);

	p->pid = -1;
	p->flags &= ~(P_SIGTERM | P_SIGKILL | P_SIGSTOP);
}

/* Termination by certain signals is not considered a failure,
   because it is nearly always the user who sends the signal. */

int badsignal(int sig)
{
	switch(sig) {
		case SIGINT: return 0;
		case SIGTERM: return 0;
		default: return 1;
	}
}

void checkfailacts(struct initrec* p, int status)
{
	int failed;

	if(WIFSIGNALED(status))
		failed = badsignal(WTERMSIG(status));
	else
		failed = WEXITSTATUS(status);

	if(p->flags & C_DTF) {

		int mintime = (p->flags & C_FAST ? TIME_TO_RESTART : MINIMUM_RUNTIME);
		int toofast = (passtime - p->lastrun <= mintime);

		/* fast respawning only counts when exit status is nonzero
		   if C_DOF is set; without C_DOF, all exits are counted. */
		if(p->flags & C_DOF)
			failed = toofast && failed;
		else
			failed = toofast;

		if(!failed) {
			p->flags |= P_WAS_OK;
			return;
		} else if(p->flags & P_WAS_OK) {
			p->flags &= ~P_WAS_OK;
			return;
		}
	};

	if(failed) {
		warn("%s[%i] failed, disabling", p->name, p->pid);
		p->flags |= P_FAILED;
	}
}
