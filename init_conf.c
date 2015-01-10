#include <string.h>
#include <stddef.h>
#include "config.h"
#include "init.h"
#include "init_conf.h"
#include "sys.h"

/* How reconfiguring works:
   	0. newblock is mmapped
	1. new config is compiled into newblock
	2. processes not in the new inittab are stopped
	3. remaining pids are transferred from inittab to newblock
	4. newblock replaces inittab, cfgblock is munmapped
   Note that up until step 4 init uses the old inittab structure, because
   step 2 implies waiting for pids that have no place for them in newtab.

   This file only handles step 1.
   configure() is the entry point. It sets up newblock memory block and
   returns 0 on success.

   In case init gets another reconfigure request while in the main
   loop during step 2, compiled newtab is discarded and we're back to step 1. */

/* Note that until rewirepointers() call late in the process, all pointers
   in struct config, struct initrec and envp aren't real pointers,
   they are offsets from the start of newblock.
   This is to avoid a lot of hassle in case mremap changes the block address. */

extern int state;
extern int currlevel;
extern struct config* cfg;

/* default/built-in stuff */
const char* inittab = INITTAB;
const char* initdir = INITDIR;

struct memblock cfgblock = { NULL };
struct memblock newblock = { NULL };

/* top-level functions handling configuration */
int readinittab(const char* file, int strict);		/* /etc/inittab */
int readinitdir(const char* dir, int strict);		/* /etc/rc */

static void initcfgblocks(void);	/* set initial values for struct config */
static int finishinittab(void);		/* copy the contents of newenviron to newblock */
static void rewirepointers(void);	/* turn offsets into actual pointers in newblock,
					   assuming it won't be mremapped anymore */
static void transferpids(void);
extern struct initrec* findentry(const char* name);

extern int mmapblock(struct memblock* m, int size);
extern void munmapblock(struct memblock* m);
extern int addptrsarray(offset listoff, int terminate);

int configure(int strict)
{
	if(mmapblock(&newblock, IRALLOC + sizeof(struct config) + sizeof(struct scratch)))
		goto unmap;

	initcfgblocks();

	if(readinittab(inittab, strict))
		/* readinittab does warn() about the reasons, so no need to do it here */
		goto unmap;
#ifdef INITDIR
	if(readinitdir(initdir, strict))
		goto unmap;
#endif

	if(finishinittab())
		goto unmap;

	rewirepointers();

	return 0;

unmap:	munmapblock(&newblock);
	return -1;
}

/* newconfig contains complied config block. Set it as mainconfig,
   freeing the old one if necessary.
   PID data transfer also occurs here; the reason is that it must
   be done as close to exchanging pointers as possible, to avoid
   losing dead children in process */
void setnewconf(void)
{
	transferpids();
	munmapblock(&cfgblock);		/* munmapblock can handle empty blocks */

	cfgblock = newblock;
	cfg = (struct config*) cfgblock.addr;
	state &= ~S_RECONFIG;
	newblock.addr = NULL;
}

static void initcfgblocks(void)
{
	struct config* cfg = newblockptr(0, struct config*);

	/* newblock has enough space for struct config, see configure() */
	int nblen = sizeof(struct config) + sizeof(struct scratch);
	newblock.ptr += nblen;
	memset(newblock.addr, 0, nblen);

	cfg->inittab = NULL;
	cfg->env = NULL;

	cfg->time_to_restart = 1;
	cfg->time_to_SIGKILL = 2;
	cfg->time_to_skip = 5;
	cfg->slippery = SLIPPERY;

	cfg->logdir = NULL;
}

static int finishinittab(void)
{
	offset off;

	if((off = addptrsarray(TABLIST, NULL_BOTH)) < 0)
		return -1;
	else
		NCF->inittab = NULL + off;

	if((off = addptrsarray(ENVLIST, NULL_BACK)) < 0)
		return -1;
	else
		NCF->env = NULL + off;

	NCF->initnum = SCR->inittab.count;
	
	return 0;
}

/* parseinittab() fills all pointers in initrecs with offsets from newblock
   to allow using MREMAP_MAYMOVE. Once newblock and newenviron are all
   set up, we need to make those offsets into real pointers */
static inline void* repoint(void* p)
{
	if(p - NULL > newblock.ptr)
		return NULL;	// XXX: should never happen
	return p ? (newblock.addr + (p - NULL)) : p;
}

#define REPOINT(a) a = repoint(a)

/* Warning: while NCF->inittab, NCF->env and initrec.argv-s within inittab
   are arrays of pointers, the fields of NCF are pointers themselves but initrec.argv is not.

   Thus, the contents of all three must be repointed (that's rewireptrsarray)
   but initrec.argv must not be touched, unlike NCF->inittab and NCF->env.

   Since char** or initrec** are not cast silently to void**, there are explicit casts here
   which may mask compiler warnings. */

static void rewireptrsarray(void** a)
{
	void** p;

	for(p = a; *p; p++)
		REPOINT(*p);
}

/* Run repoint() on all relevant pointers within newblock */
static void rewirepointers()
{
	struct initrec** pp;

	REPOINT(NCF->inittab);
	rewireptrsarray((void**) NCF->inittab);

	REPOINT(NCF->env);
	rewireptrsarray((void**) NCF->env);

	for(pp = NCF->inittab; *pp; pp++)
		rewireptrsarray((void**) (*pp)->argv);
}

/* move child state info from cfgblock to newblock */
/* old inittab is cfg->inittab (which may or may not be CFG->inittab) */
/* new inittab is NCF->inittab */
static void transferpids(void)
{
	struct initrec* p;
	struct initrec* q;
	struct initrec** qq;

	for(qq = NCF->inittab; (q = *qq); qq++) {
		/* Prevent w-type entries from being spawned during
		   the next initpass() just because they are new */
		/* This requires (currlevel == nextlevel) which is enforced with S_RECONF. */
		if((q->flags & C_WAIT) && (q->rlvl & currlevel))
			q->pid = -1;

		if(!cfg) /* boot-time configure, no inittab to transfer pids from */
			continue;

		if(!q->name) /* can't transfer unnamed entries */
			continue;

		if(!(p = findentry(q->name))) /* the entry is new, nothing to transfer here */
			continue;

		q->pid = p->pid;
		q->flags |= (p->flags & (P_SIGTERM | P_SIGKILL | P_ZOMBIE));
		q->lastrun = p->lastrun;
		q->lastsig = p->lastsig;
	}
}
