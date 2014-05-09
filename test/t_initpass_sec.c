#include "../init.h"
#include "test.h"

extern int state;
extern int currlevel;
extern int nextlevel;

extern char passlog[];
extern void reset(void);
extern void died(struct initrec* p);
extern void initpass(void);

struct initrec I0;
struct initrec I1;
struct initrec I2;
struct initrec I3;
struct initrec I4;
struct initrec I5;
struct initrec I6;

struct config testconfig = { .inittab = &I0 };
struct config* cfg = &testconfig;

#define R1 (1<<1)
#define R2 (1<<2)
#define Ra (1<<0xa)
#define Rb (1<<0xb)
#define SM SUBMASK

struct initrec I0 = { .next = &I1,  .prev = NULL,.pid = 0, .name = "i0", .flags = C_ONCE, .rlvl = R1 };
struct initrec I1 = { .next = &I2,  .prev = &I0, .pid = 0, .name = "i1", .flags = C_ONCE | C_WAIT, .rlvl = R1 | R2 | Ra };
struct initrec I2 = { .next = &I3,  .prev = &I1, .pid = 0, .name = "i2", .flags = 0,      .rlvl = R1 | R2 | Ra | Rb };
struct initrec I3 = { .next = &I4,  .prev = &I2, .pid = 0, .name = "i3", .flags = 0,      .rlvl = R1 | Ra };
struct initrec I4 = { .next = NULL, .prev = &I3, .pid = 0, .name = "i4", .flags = 0,      .rlvl = R1 };

#define Q(t) { reset(); initpass(); S(passlog, t); }
#define Qq(t) Q(t); Q("")
#define D(i) died(&i)
#define K(i) killed(&i)
#define N(r) nextlevel = r

int main(void)
{
	currlevel = 0;
	nextlevel = R1;
	state = 0;

	/* bootup */
	Qq("+i0+i4"); D(I0); Q("");

	nextlevel |= Ra;
	Qq("+i1"); D(I1); Qq("+i2+i3");
	nextlevel |= Rb;
	Q("");
	nextlevel &= ~Ra;
	Q("-i3"); D(I3); Q("");

	nextlevel = (nextlevel & SUBMASK) | R2;
	Q("-i4"); D(I4); Q("");
	
	return 0;
}
