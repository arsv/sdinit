#define _GNU_SOURCE
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/open.h>
#include <sys/close.h>
#include <sys/getdents.h>
#include "config.h"
#include "init.h"
#include "init_conf.h"
#include "scope.h"
#include "sys.h"

export int readinitdir(const char* dir, int strict);

extern int addinitrec(const char* name, const char* rlvl, char* cmd, int exe);

extern struct fblock fileblock;
extern int mmapfile(const char* name, int maxlen);
extern int munmapfile(void);
extern char* nextline(void);

local int skipdirent(struct dirent64* de);
local int parsesrvfile(char* fullname, char* basename);
local int comment(const char* s);

/* Initdir is one-file-per-entry structure, while inittab is
   one-line-per-entry. Other than that, they are very similar.
   Both call addinitrec() the same way.

   Only the proper contents of INITDIR is checked, with no recursion.
   This simplifies the code *and* allows storing auxilliary scripts
   in a directory under INITDIR without init trying to pick them up.

   File basename is used as the entry name for initdir entries.

   Just like with readinitdir, the files are mmaped whole.
   Here however init may come across a large script of which only
   the two initial lines are needed to make an initrec, so there is
   a limit on the mmaped block size.
   The assumption is that raw entries (non-scripts) must be short. */

int readinitdir(const char* dir, int strict)
{
	int dirfd;
	struct dirent64* de;
	int nr, ni;		/* getdents ret and index */
	int ret = -1;

	bss char debuf[DENTBUFSIZE];
	bss char fname[FULLNAMEMAX];
	const int delen = sizeof(debuf);
	const int fnlen = sizeof(fname);
	int bnoff;		/* basename offset in fname */
	int bnlen;

	/*        |            |<---- bnlen ---->| */
	/* fname: |/path/to/dir/filename         | */
	/*        |--- bnoff ---^                  */
	strncpy(fname, dir, fnlen - 1);
	bnoff = strlen(fname);
	fname[bnoff++] = '/';
	bnlen = fnlen - bnoff;

	if((dirfd = sysopen(dir, O_RDONLY | O_DIRECTORY)) < 0) {
		if(dirfd == -ENOENT)
			return 0;
		else
			retwarn(ret, "Can't open %s", dir);
	}

	while((nr = sysgetdents64(dirfd, (void*)debuf, delen)) > 0) {
		for(ni = 0; ni < nr; ni += de->d_reclen) {
			de = (struct dirent64*)(debuf + ni);

			if(skipdirent(de))
				continue;

			strncpy(fname + bnoff, de->d_name, bnlen);

			if(!(ret = mmapfile(fname, 1024))) {
				ret = parsesrvfile(fname, de->d_name);
				munmapfile();
			} if(ret && strict)
				goto out;
			else if(ret)
				warn("%s: skipping %s", dir, de->d_name);
		}
	}

	ret = 0;
out:	sysclose(dirfd);
	return ret;
}

/* Some direntries in initdir should be silently ignored */
int skipdirent(struct dirent64* de)
{
	char dt;
	int len = strlen(de->d_name);

	/* skip hidden files */
	if(de->d_name[0] == '.')
		return 1;

	/* skip temp files */
	if(len > 0 && de->d_name[len - 1] == '~')
		return 1;

	/* skip uppercase files (this is bad but keeps README out for now) */
	if(len > 0 && de->d_name[0] >= 'A' && de->d_name[0] <= 'Z')
		return 1;

	/* skip non-files early if the kernel was kind enough to warn us */
	if((dt = de->d_type) && dt != DT_LNK && dt != DT_REG)
		return 1;

	return 0;
}

/* The default values for flags and runlevels are chosen so that
   the most typical respawning entries would not need to specify
   either explicitly. For initdir entries, flags are optional.

   With initdir entries, there is also the special case of executable
   scripts. Regular entries are compiled like this:
        cmd = `cat INITDIR/somefile`
   but with executable scripts, we do
        cmd = INITDIR/somescript
   instead, leaving the parsing job mostly to the shebang interpreter.

   This makes it possible to use regular shell scripts as init entries
   directly, without the need for extra shims to call them.

   Whenever explicit flags are needed, the go in a #-comment line,
   which must be line 1 for raw entries and line 2 for script
   (scripts have shebang for line 1). */

int comment(const char* s)
{
	while(*s == ' ' || *s == '\t') s++;
	return !*s || *s == '#';
}

int parsesrvfile(char* fullname, char* basename)
{
	int shebang = 0;
	char* cmd;
	const char* rlvls = "S";
	char* ls;

	ls = nextline();

	/* Check for, and skip #! line if present */
	if(ls && ls[0] == '#' && ls[1] == '!') {
		shebang = 1;
		ls = nextline();
	}

	/* Same for #: runlevels line */
	if(ls && ls[0] == '#' && ls[1] == ':') {
		ls[1] = *rlvls;
		rlvls = ls + 1;
		ls = nextline();
	}

	if(shebang) {
		/* No need to parse anything anymore, it's a script. */
		cmd = fullname;
	} else {
		/* Get to first non-comment line, and that's it, the rest
		   will be done in addinitrec. */
		while(ls && comment(ls))
			ls = nextline();
		cmd = ls;
	}

	if(!ls) /* could be just cmd, but empty scripts are not ok */
		retwarn(-1, "%s: no command found", FBN);

	return addinitrec(basename, rlvls, cmd, shebang);
}
