#include "../config.h"
#include "../init.h"
#include "../init_conf.h"
#include "_test.h"

#define R0 (1<<0)
#define R1 (1<<1)
#define R2 (1<<2)
#define R3 (1<<3)
#define R4 (1<<4)
#define R5 (1<<5)
#define R6 (1<<6)
#define R7 (1<<7)
#define R8 (1<<8)
#define R9 (1<<9)
#define Ra (1<<0xa)
#define Rb (1<<0xb)
#define Rf (1<<0xf)

struct fblock fileblock = {
	.name = "(none)",
	.line = 0,
	.buf = NULL,
	.ls = NULL,
	.le = NULL
};
struct initrec rec = {
	.rlvl = 0
};

#define right(str) \
	ZERO(setrunflags(&rec, heapdup(str)))

#define wrong(str) \
	ZERO(!setrunflags(&rec, heapdup(str)))

#define levels(str, exp) \
	right(str);\
	HEXEQUALS(rec.rlvl, exp)

#define flags(str, exp) \
	right(str);\
	HEXEQUALS(rec.flags, exp)

#define both(str, ef, er) \
	right(str);\
	HEXEQUALS(rec.rlvl, er);\
	HEXEQUALS(rec.flags, ef)

int main(void)
{
	/* First check that the flags work */
	flags("H",	C_HUSH);
	flags("R",	C_ONCE);
	flags("W",	C_ONCE | C_WAIT);
	flags("L",	C_WAIT);

	/* regular runlevel specification */
	levels("R12",	R1 | R2);
	levels("R9",	R9);
	levels("R0",	R0);

	/* the + sign */
	levels("R3*",	R3 | R4 | R5 | R6 | R7 | R8 | R9);
	levels("R5*",	R5 | R6 | R7 | R8 | R9);

	/* mix of pri and sub levels */
	levels("L12a",	R1 | R2 | Ra);
	levels("S12af",	R1 | R2 | Ra | Rf);

	/* mix of pri, sub levels with + */
	levels("R7ab*",	R7 | R8 | R9 | Ra | Rb);

	/* missing levels */
	levels("R",	DEFAULTMASK);
	levels("R*",	R1 | R2 | R3 | R4 | R5 | R6 | R7 | R8 | R9);

	/* Runlevels and flags together */
	both("R3*",	C_ONCE, R3 | R4 | R5 | R6 | R7 | R8 | R9);
	both("X3*",	C_ONCE | C_INVERT, R3 | R4 | R5 | R6 | R7 | R8 | R9);
	/* only the primary levels are inverted by X */
	both("X3a*",	C_ONCE | C_INVERT, R3 | R4 | R5 | R6 | R7 | R8 | R9 | Ra);

	/* Incorrect strings that should fail */
	wrong("");	/* no empty strings */
	wrong("-");	/* no "default" settings for now */
	wrong("Z");	/* unknown mode */
	wrong("123");	/* bare runlevels */
	wrong("R12q");	/* bad runlevel spec */

	return 0;
}
