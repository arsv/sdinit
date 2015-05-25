#ifndef WAIT_H
#define WAIT_H

#include <sys/types.h>

#define	WNOHANG		1	/* Don't block waiting.  */
#define WUNTRACED	2	/* Report stopped child. */
#define WCONTINUED	8	/* Report continued child.  */

#define WEXITSTATUS(status)	(((status) & 0xFF00) >> 8)
#define WTERMSIG(status)	((status) & 0x7F)

#define WIFEXITED(status)	(WTERMSIG(status) == 0)
#define WIFSIGNALED(status)	(!WIFSTOPPED(status) && !WIFEXITED(status))
#define WIFSTOPPED(status)	(((status) & 0xFF) == 0x7F)
#define WIFCONTINUED(status)	((status) == 0xFFFF)

pid_t waitpid(pid_t pid, int *status, int options);

#endif
