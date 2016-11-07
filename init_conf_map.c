#define _GNU_SOURCE
#include <sys/mmap.h>
#include <sys/munmap.h>
#include <sys/mremap.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/close.h>
#include <string.h>
#include <fcntl.h>
#include "init.h"
#include "init_conf.h"
#include "scope.h"

/* Init uses mmap to allocate memory *and* to read files.
   At any given point, there are at most three mmaped areas: */

struct cblock cfgblock;         /* active configuration */
struct nblock newblock;         /* newly compiled configuration */
struct fblock fileblock;        /* the file being parsed */

/* New configuration is built by appending structures of known size
   to newblock. If reconfiguration succeeds, the blocks are exchanged:
   old cfgblock gets unmmaped and newblock becomes cfgblock.
   In case configuration fails, we just unmmap newblock. */

export int mmapblock(int size);
export offset extendblock(int size);
export void exchangeblocks(void);
export void munmapblock(void);

/* The file block is used to parse inittab or service files linewise.
   Filename is stored in the structure for error reporting.
   The pointer must be usable up until munmapfile is called. */

export int mmapfile(const char* filename, int maxlen);
export int munmapfile(void);
export char* nextline(void);

/* The block is initially allocated to hold struct config and
   struct scratch. Initrecs are added later with extendblock.

   The only values of size ever used are sizeof(struct config) +
   sizeof(struct scratch) in init_conf.c and 0 in tests, so there
   is little point in check them.

   There is a relatively unlikely case when a new reconfigure request
   comes before newblock from the previous one is moved over to cfgblock.
   In such a case, we re-use newblock without unmmaping it.
   Again, it is always large enough, so the check is really redundant. */

int mmapblock(int size)
{
	if(size > PAGESIZE)
		return -1;

	if(newblock.addr) {
		if(newblock.len < size)
			return -1;
	} else {
		const int prot = PROT_READ | PROT_WRITE;
		const int flags = MAP_PRIVATE | MAP_ANONYMOUS;

		void* addr = (void*)sysmmap(NULL, PAGESIZE, prot, flags, -1, 0);

		if(addr == MAP_FAILED)
			return -1;

		newblock.addr = addr;
		newblock.len = PAGESIZE;
	}

	memset(newblock.addr, 0, newblock.len);
	newblock.ptr = size;

	return 0;
}

/* newblock.ptr tracks the start of empty space in newblock.
   We make sure there is enough space, and allocate it by moving ptr over. */

offset extendblock(int size)
{
	int ret = newblock.ptr;

	int rem = (newblock.len - newblock.ptr); /* space remaining */
	int add = size - rem;			 /* space to be added */

	if(add <= 0)
		goto moveptr;
	if(add % PAGESIZE)
		add += (PAGESIZE - add % PAGESIZE);
	
	int oldlen = newblock.len;
	int newlen = oldlen + add;

	if(newlen < oldlen)
		return -1;	/* overflow? */

	void* newaddr = (void*)sysmremap(newblock.addr, oldlen, newlen, MREMAP_MAYMOVE);

	if(newaddr == MAP_FAILED)
		return -1;

	newblock.addr = newaddr;
	newblock.len = newlen;
moveptr:
	newblock.ptr += size;

	return ret;
}

void munmapblock(void)
{
	sysmunmap(newblock.addr, newblock.len);

	newblock.addr = 0;
	newblock.len = 0;
};

/* Normally the old block in unmmaped and the new one takes its place.
   However, cfgblock may happen be empty on the first reconfig attempt
   if the old config is in fact builtin config. */

void exchangeblocks(void)
{
	if(cfgblock.addr)
		sysmunmap(cfgblock.addr, cfgblock.len);

	cfgblock.addr = newblock.addr;
	cfgblock.len = newblock.len;

	newblock.addr = NULL;
	newblock.len = 0;
};

/* Due to average inittab being about 1-2k, it is always read whole;
   for service files, only the head is mmaped.
   Init makes no distinction between mmap failure and open failure,
   both mean the new inittab won't be used.

	maxlen > 0: error if the file is larger (inittab)
	maxlen < 0: map at most -maxlen first bytes (initdir)

   The result is always 0-terminated. */

int mmapfile(const char* filename, int maxlen)
{
	struct stat st;

	int fd = sysopen(filename, O_RDONLY);
	if(fd < 0)
		retwarn(-1, "can't open %s: %m", filename);

	if(sysfstat(fd, &st) < 0)
		gotowarn(out, "can't stat %s: %m", filename);

	if(!S_ISREG(st.st_mode))
		gotowarn(out, "%s: not a regular file", filename);
	if(maxlen < 0 && st.st_size > -maxlen)
		/* Because ints are used in lots of places,
		   it is a good idea to avoid loading anything that
		   exceeds 2^31 when compiled into newblock */
		gotowarn(out, "%s: file too large", filename);

	int stm = st.st_size;
	if(maxlen > 0 && stm > maxlen) stm = maxlen;

	/* with one guard byte at the end, to hold \0 */
	const int prot = PROT_READ | PROT_WRITE;
	const int flags = MAP_PRIVATE;
	char* addr = (char*)sysmmap(NULL, stm + 1, prot, flags, fd, 0);

	if(addr == MAP_FAILED)
		gotowarn(out, "%s: mmap failed: %m", filename);

	addr[stm] = '\0';

	fileblock.buf = addr;
	fileblock.len = stm;
	fileblock.ls = NULL;
	fileblock.le = NULL;
	fileblock.name = filename;
	fileblock.line = 0;

	sysclose(fd);
	return 0;

out:	sysclose(fd);
	return -1;
}

int munmapfile(void)
{
	return sysmunmap(fileblock.buf, fileblock.len);
}

/* The file is mmaped rw and private. We place the pointers
   and terminate the line with \0, overwriting \n. */

char* nextline(void)
{
	char* le = fileblock.le;
	char* ls = le ? le + 1 : fileblock.buf;
	char* end = fileblock.buf + fileblock.len;

	if(ls >= end) return NULL;

	for(le = ls; le < end && *le != '\n'; le++)
		; /* clang is full of hatred towards elegant concise expressions */
	*le = '\0';

	fileblock.ls = ls;
	fileblock.le = le;
	fileblock.line++;

	return ls;
}
