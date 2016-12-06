#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include "../config.h"
#include "../init.h"
#include "../init_conf.h"
#include "_test.h"

/* Run (almost) full config sequence and check whether newblock is being formed properly */

struct config* cfg = NULL;
int currlevel = 0;

#define HEAP 1024
char heap[HEAP];

int levelmatch(struct initrec* p, int level)
{
	return 0;
}

struct initrec* findentry(const char* name)
{
	return NULL;
}

int parseinitline_(char* testline)
{
	char* line = strncpy(heap, testline, HEAP);
	return parseinitline(line);
}

int checkptr(void* ptr)
{
	return (ptr >= newblock.addr && ptr < newblock.addr + newblock.len);
}

#define blockoffset(ptr) ( (int)((void*)(ptr) - newblock.addr) )

void dumpenv(struct config* cfg)
{
	char** p;

	printf("ENV: %p [%i]\n", cfg->env, blockoffset(cfg->env));
	for(p = cfg->env; p && *p; p++) {
		if(!checkptr(*p))
			die("ENV %p bad pointer\n", p);
		printf("  [%i] -> [%i] %s\n",
				blockoffset(p),
				blockoffset(*p),
				*p ? *p : "NULL");
	}
}

void dumptab(struct config* cfg)
{
	struct initrec *q, **qq;
	char** p;
	int i;

	printf("TAB: %p [%i]\n", cfg->inittab, blockoffset(cfg->inittab));
	for(qq = cfg->inittab; (q = *qq); qq++) {
		printf("  [%i] name=\"%s\" rlvl=%i flags=%i\n",
				blockoffset(q), q->name, q->rlvl, q->flags);
		for(i = 0, p = q->argv; p && *p; p++, i++)
			if(checkptr(*p))
				printf("\targv[%i] -> [%i] \"%s\"\n", i, blockoffset(*p), *p);
			else
				printf("\targv[%i] BAD %p\n", i, *p);
	}
}

void dumpconfig(void)
{
	struct config* cfg = NCF;

	ASSERT(checkptr(cfg));
	printf("NCF: %p [%i]\n", cfg, blockoffset(cfg));

	dumptab(cfg);
	dumpenv(cfg);
}

int main(void)
{
	ZERO(mmapblock(sizeof(struct config) + sizeof(struct scratch)));

	ZERO(parseinitline_("# comment here"));
	ZERO(parseinitline_(""));
	ZERO(parseinitline_("FOO=something"));
	ZERO(parseinitline_("PATH=/bin:/sbin:/usr/bin"));
	ZERO(parseinitline_(""));
	ZERO(parseinitline_("time    W12345   /sbin/hwclock -s"));
	ZERO(parseinitline_("mount   W12345   /bin/mount -a"));

	ZERO(finishinittab())
	rewirepointers();

	dumpconfig();
	
	return 0;
}

void die(const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	kill(getpid(), SIGUSR1);
	_exit(-1);
};
