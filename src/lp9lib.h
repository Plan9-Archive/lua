#include <u.h>
#include "lauxlib.h"

#include </sys/src/ape/lib/ap/plan9/lib.h>
#include </sys/src/ape/lib/ap/plan9/sys9.h>
#include </sys/src/ape/lib/ap/plan9/dir.h>

#pragma	lib	"libc.a"
#pragma	src	"/sys/src/libc"

typedef
struct Tm
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
	int	wday;
	int	yday;
	char	zone[4];
	int	tzoff;
} Tm;

typedef
struct Lock {
	int	val;
} Lock;

extern	int	close(int);
extern	long	dirreadall(int, Dir**);
extern	Dir*	dirstat(char*);
extern	int   snprint(char *s, int len, char *format, ...);
extern	int	getfields(char*, char**, int, int, char*);
extern	char*	seprint(char*, char*, char*, ...);
extern	int	chartorune(Rune*, char*);
extern	int	runetochar(char*, Rune*);
extern	Tm*	gmtime(long);
extern	int access(char *name, int mode);
extern	int create(char *file, int omode, ulong perm);
extern	void genrandom(char *buf, int nbytes);
extern	vlong seek(int fd, vlong off, int how);
extern	int fork(void);
extern	Waitmsg*	_WAIT(void);
extern	int atnotify(int (*f)(void*, char*), int in);
extern	int fprint(int fd, char *format, ...);
extern	void lock(Lock *lk);
extern	void unlock(Lock *lk);

/* network routines */
extern	int	accept(int, char*);
extern	int	announce(char*, char*);
extern	int	dial(char*, char*, char*, int*);
extern	int	hangup(int);
extern	int	listen(char*, char*);
extern	char*	netmkaddr(char*, char*, char*);
extern	int	reject(int, char*, char *);

enum
{
	UTFmax		= 3,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
};
