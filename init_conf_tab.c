#include <string.h>
#include "init.h"
#include "init_conf.h"
#include "scope.h"

export int readinittab(const char* file, int strict);

extern int addinitrec(char* code, char* name, char* cmd, int exe);
extern int addenviron(const char* string);

extern struct fileblock fb;
extern int mmapfile(const char* filename, int maxlen);
extern int munmapfile(void);
extern char* nextline(void);

local int parseinitline(char* line);
extern char* strssep(char** str);

/* Typical lines:

	# comment
	VARIABLE=value

	mount    W1+    /sbin/mount -a
	         R3+    /sbin/net up

   Leading whitespace is siginificant, means unnamed entry.

   strict=1: bail out on errors immediately
   strict=0: ignore errors, used during startup */

int readinittab(const char* file, int strict)
{
	int ret = -1;
	char* ls;

	if(mmapfile(file, -MAXFILE))
		return -1;

	while((ls = nextline()))
		if((ret = parseinitline(ls)) && strict)
			break;

	munmapfile();

	return strict ? ret : 0;
}

int parseinitline(char* l)
{
	char* p;

	if(!*l || *l == '#')
		return 0;

	if(!(p = strpbrk(l, "= \t:")))
		goto bad;
	else if(*p == ':')
		retwarn(-1, "%s:%i: SysV-style inittab detected, aborting", fb.name, fb.line);
	else if(*p == '=')
		return addenviron(l);

	char* name = strssep(&l);
	char* rlvl = strssep(&l);
	/* l is the command here */

	if(name && rlvl && *l)
		return addinitrec(name, rlvl, l, 0);

bad:	retwarn(-1, "%s:%i: bad line", fb.name, fb.line);
}
