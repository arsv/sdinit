/* Simple UTC imlementation of timestamp() a-la musl.
   Timezone is not loaded, kernel time is used directly */

/* Accidentally, this version of timestamp() is also perfectly
   correct in case kernel uses local time instead of UTC */

#include <time.h>

extern int mktimestamp(char* p, int l, time_t ts);

int timestamp(char* buf, int len)
{
	struct timespec tp = { 0, 0 };
	clock_gettime(CLOCK_REALTIME, &tp);
	return mktimestamp(buf, len, tp.tv_sec);
}
