/**************************************************
UZI (Unix Z80 Implementation) Kernel:  scall2.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern void	bcopy(const void *, void *, int);
extern void	bzero(void *, int);

int		_getpid(void);
int		_getppid(void);
int		_getuid(void);
int		_getgid(void);
int		_setuid(int);
int		_setgid(int);
int		_time(int *);
int		_stime(int *);
int		_times(char *);
int		_execve(char *, char **, char **);
int		_brk(char *);
int		_sbrk(uint16);
int		_wait(int *);
void		__exit(int16);
int		_fork(void);
int		_pause(void);
int		_signal(int16, int16 (*func)());
int		_kill(int16, int16);
int		_alarm(uint16);

void		doexit(int16, int16);

static void	exec2(void);
static int	wargs(char **, int);
static char *	rargs(char *, int, int *);


/* getpid() */
int
_getpid(void)
{
	return (udata.u_ptab->p_pid);
}

/* getppid() */
int
_getppid(void)
{
	return (udata.u_ptab->p_pptr->p_pid);
}

/* getuid() */
int
_getuid(void)
{
	return (udata.u_ptab->p_uid);
}

/* getgid() */
int
_getgid(void)
{
	return (udata.u_gid);
}

/*********************************
setuid(int uid)
***********************************/
int
_setuid(int uid)
{
	uid = (int)udata.u_argn;

	if (super() || udata.u_ptab->p_uid == uid) {
		udata.u_ptab->p_uid = uid;
		udata.u_euid = uid;
		return (0);
	}
	udata.u_error = EPERM;
	return (-1);
}

/*****************************************
setgid(int gid)
****************************************/
int
_setgid(int gid)
{
	gid = (int16)udata.u_argn;

	if (super() || udata.u_gid == gid) {
		udata.u_gid = gid;
		udata.u_egid = gid;
		return (0);
	}
	udata.u_error = EPERM;
	return (-1);
}

/***********************************
time(int tvec[])
**************************************/
int
_time(int tvec[])
{
	tvec = (int *)udata.u_argn;

	rdtime(tvec);	/* In machdep.c */
	return (0);
}

/**************************************
stime(int tvec[])
**********************************/
int
_stime(int tvec[])
{
	tvec = (int *)udata.u_argn;

/* XXX - Why is this commented out
	ifnot (super()) {
		udata.u_error = EPERM;
		return (-1);
	}
	sttime(tvec);
	return (0);
*/

	udata.u_error = EPERM;
	return (-1);
}

/********************************************
times(char *buf)
**********************************************/
int
_times(char *buf)
{
	buf = (char *)udata.u_argn;

	ifnot (valadr(buf, 6 * sizeof(time_t)))
		return (-1);

	di();
	bcopy(&udata.u_utime, buf, 4 * sizeof(time_t));
	bcopy(&ticks, buf + 4 * sizeof(time_t), sizeof(time_t));
	ei();
	return (0);
}

/* User's execve() call. All other flavors are library routines. */

/*****************************************
execve(char *name, char *argv[], char *envp[])
*****************************************/
int
_execve(char *name, char *argv[], char *envp[])
{
	name = (char *)udata.u_argn2;
	argv = (char **)udata.u_argn1;
	envp = (char **)udata.u_argn;

	inoptr ino;
	char *buf;
	inoptr n_open();
	char *bread();
	blkno_t bmap();

	ifnot (ino = n_open(name, NULLINOPTR))
		return (-1);

	if (ino->c_node.i_size.o_blkno >= ((uint16)(&udata) / 512)) {
		udata.u_error = ENOMEM;
		goto nogood;
	}

	ifnot ((getperm(ino) & OTH_EX) && (ino->c_node.i_mode & F_REG) &&
	    (ino->c_node.i_mode & (OWN_EX | OTH_EX | GRP_EX))) {
		udata.u_error = EACCES;
		goto nogood;
	}

	setftime(ino, A_TIME);

	/*
	 * Gather the arguments and put them on the root device.
	 * Put environment on another block.
	 */
	if (wargs(argv, 0) || wargs(envp, 1))
		goto nogood;

	/* Read in the first block of the new program. */
	buf = bread( ino->c_dev, bmap(ino, 0, 1), 0);

	if ((*buf & 0xff) != EMAGIC) {
		udata.u_error = ENOEXEC;
		goto nogood2;
	}

	/*
	 * Here, check the setuid stuff.
	 * No other changes need to be made in the user data.
	 */
	if (ino->c_node.i_mode & SET_UID)
		udata.u_euid = ino->c_node.i_uid;

	if (ino->c_node.i_mode & SET_GID)
		udata.u_egid = ino->c_node.i_gid;

	bcopy(buf, PROGBASE, 512);
	bfree(buf, 0);

	/*
	 * At this point, we are committed to reading in and
	 * executing the program.  We switch to a local stack,
	 * and pass to it the necessary parameter: ino.
	 */
	udata.u_ino = ino;	/* Termorarily stash these here. */

	tempstack();
	exec2();	/* Never returns. */
nogood2:
	bfree(buf, 0);
nogood:
	i_deref(ino);
	return (-1);
}

static void
exec2(void)
{
	blkno_t blk;
	char **argv;
	char **envp;
	int (**sp)();
	int argc;
	char *rargs();
	char *progptr;
	char *buf;
	blkno_t pblk;
	blkno_t bmap();
	char *bread();

	/* Read in the rest of the program. */
	progptr = PROGBASE + 512;
	for (blk = 1; blk <= udata.u_ino->c_node.i_size.o_blkno; ++blk) {
		pblk = bmap(udata.u_ino, blk, 1);
		if (pblk != -1) {
			buf = bread( udata.u_ino->c_dev, pblk, 0);
			bcopy(buf, progptr, 512);
			bfree(buf, 0);
		}
		progptr += 512;
	}
	i_deref(udata.u_ino);

	/* Zero out the free memory. */
	bzero(progptr, (uint16)((char *)&udata - progptr));
	udata.u_break = progptr;

	/* Read back the arguments and the environment. */
	argv = (char **)rargs((char *)&udata, 0, &argc);
	envp = (char **)rargs((char *)argv, 1, NULL);

	/* Fill in udata.u_name. */
	bcopy(*argv, udata.u_name, 8);

	/* Turn off caught signals. */
	for (sp = udata.u_sigvec; sp < (udata.u_sigvec + NSIGS); ++sp)
		if (*sp != SIG_IGN)
			*sp = SIG_DFL;

	/* Shove argc and the address of argv just below envp. */
	*(envp - 1) = (char *)argc;
	*(envp - 2) = (char *)argv;

	/* Go jump into the program, first setting the stack. */
	doexec((int16 *)(udata.u_isp = envp - 2));
}

static int
wargs(char **argv, int blk)
{
	/* Address of base of arg strings in user space. */
	char *ptr;
	int n;
	struct s_argblk *argbuf;
	char *bufp;
	int j;
	char *zerobuf();
	char *bread();

	/* Gather the arguments and put them on the swap device. */
	argbuf = (struct s_argblk *)bread(SWAPDEV,
	    udata.u_ptab->p_swap + blk, 2);

	bufp = argbuf->a_buf;
	for (j = 0; argv[j] != NULL; ++j) {
		ptr = argv[j];
		do {
			*bufp++ = *ptr;
			if (bufp >= argbuf->a_buf + 500) {
				udata.u_error = E2BIG;
				bfree((char *)argbuf, 1);
				return (1);
			}
		} while (*ptr++ != '\0');
	}

	argbuf->a_argc = j;	/* Store argc in argbuf. */
	/* Store total string size. */
	argbuf->a_arglen = bufp - argbuf->a_buf;
	/* Swap out the arguments into the given swap block. */
	bfree((char *)argbuf, 1);

	return (0);
}

static char *
rargs(char *ptr, int blk, int *cnt)
{
	struct s_argblk *argbuf;
	char **argv;	/* Address of users argv[], just below ptr. */
	int n;
	char *bread();

	/* Read back the arguments. */
	argbuf = (struct s_argblk *)bread(SWAPDEV,
	    udata.u_ptab->p_swap + blk, 0);

	/* Move them into the users address space, at the very top. */
	ptr -= argbuf->a_arglen;
	if (argbuf->a_arglen)
		bcopy(argbuf->a_buf, ptr, argbuf->a_arglen);

	/* Set argv to point below the argument strings. */
	argv = (char **)ptr - (argbuf->a_argc + 1);

	/* Set each element of argv[] to point to its argument string. */
	argv[0] = ptr;
	for (n = 1; n < argbuf->a_argc; ++n)
		argv[n] = argv[n - 1] + strlen(argv[n - 1]) + 1;
	argv[argbuf->a_argc] = NULL;

	if (cnt)
		*cnt = argbuf->a_argc;

	bfree((char *)argbuf, 0);
	return (argv);
}

/**********************************
brk(char *addr)
************************************/
int
_brk(char *addr)
{
	addr = (char *)udata.u_argn;

	char dummy;	/* A thing to take address of. */

	/* XXX - A hack to get approx val of stack ptr. */
	if (addr < PROGBASE || (addr + 64) >= (char *)&dummy) {
		udata.u_error = ENOMEM;
		return (-1);
	}
	udata.u_break = addr;
	return (0);
}

/************************************
sbrk(uint16 incr)
***************************************/
int
_sbrk(uint16 incr)
{
	incr = (uint16)udata.u_argn;

	char *oldbrk;

	udata.u_argn += (oldbrk = udata.u_break);
	if (_brk(udata.u_argn))	/* XXX - Verify correctness. */
		return (-1);

	return ((int)oldbrk);
}

/**************************************
wait(int *statloc)
****************************************/
int
_wait(int *statloc)
{
	statloc = (int *)udata.u_argn;

	ptptr p;
	int retval;

	if (statloc > (int *)(&udata)) {
		udata.u_error = EFAULT;
		return (-1);
	}

	di();
	/* See if we have any children. */
	for (p = ptab; p < ptab + PTABSIZE; ++p) {
		if (p->p_status && p->p_pptr == udata.u_ptab &&
		    p != udata.u_ptab)
			goto ok;
	}
	udata.u_error = ECHILD;
	ei();
	return (-1);
ok:
	/* Search for an exited child. */
	for (;;) {
		chksigs();
		if (udata.u_cursig) {
			udata.u_error = EINTR;
			return (-1);
		}
		di();
		for (p = ptab; p < ptab + PTABSIZE; ++p) {
			if (p->p_status == P_ZOMBIE &&
			    p->p_pptr == udata.u_ptab) {
				if (statloc)
					*statloc = p->p_exitval;
				p->p_status = P_EMPTY;
				retval = p->p_pid;

				/*
				 * Add in child's time info.
				 * It was stored on top of p_wait in
				 * the childs process table entry.
				 */
				addtick(&udata.u_cutime, &(p->p_wait));
				addtick(&udata.u_cstime,
				    (char *)(&(p->p_wait)) + sizeof(time_t));
				ei();
				return (retval);
			}
		}
		/* Nothing yet, so wait. */
		psleep(udata.u_ptab);
	}
}

/**************************************
_exit(int16 val)
**************************************/
void
__exit(int16 val)
{
	val = (int16)udata.u_argn;

	doexit(val, 0);
}

void
doexit(int16 val, int16 val2)
{
	int16 j;
	ptptr p;

	for (j = 0; j < UFTSIZE; ++j)
		ifnot (udata.u_files[j] & 0x80)	/* Portable equiv. of == -1. */
			doclose(j);

	_sync();	/* Not necessary, but a good idea. */

	di();
	udata.u_ptab->p_exitval = (val << 8) | (val2 & 0xff);

	/* Set child's parents to init. */
	for (p = ptab; p < ptab + PTABSIZE; ++p)
		if (p->p_status && p->p_pptr == udata.u_ptab)
			p->p_pptr = initproc;
	i_deref(udata.u_cwd);

	/*
	 * Stash away child's execution tick counts in process table,
	 * overlaying some no longer necessary stuff.
	 */
	addtick(&udata.u_utime, &udata.u_cutime);
	addtick(&udata.u_stime, &udata.u_cstime);
	bcopy(&udata.u_utime, &(udata.u_ptab->p_wait), 2 * sizeof(time_t));

	/* Wake up a waiting parent, if any. */
	if (udata.u_ptab != initproc)
		wakeup((char *)udata.u_ptab->p_pptr);
	udata.u_ptab->p_status = P_ZOMBIE;
	ei();
	swapin(getproc());
	panic("doexit:won't exit");
}

int
_fork(void)
{
	return (dofork());
}

int
_pause(void)
{
	psleep(0);
	udata.u_error = EINTR;
	return (-1);
}

/*************************************
signal(int16 sig, int16 (*func)())
***************************************/
int
_signal(int16 sig, int16 (*func)())
{
	sig = (int16)udata.u_argn1;
	func = (int (*)())udata.u_argn;

	int retval;

	di();
	if (sig < 1 || sig == SIGKILL || sig >= NSIGS) {
		udata.u_error = EINVAL;
		goto nogood;
	}

	if (func == SIG_IGN)
		udata.u_ptab->p_ignored |= sigmask(sig);
	else {
		if (func != SIG_DFL && ((char *)func < PROGBASE ||
		    (struct u_data *)func >= &udata)) {
			udata.u_error = EFAULT;
			goto nogood;
		}
		udata.u_ptab->p_ignored &= ~sigmask(sig);
	}
	retval = udata.u_sigvec[sig];
	udata.u_sigvec[sig] = func;
	ei();
	return (retval);
nogood:
	ei();
	return (-1);
}

/**************************************
kill(int16 pid, int16 sig)
*****************************************/
int
_kill(int16 pid, int16 sig)
{
	pid = (int16)udata.u_argn1;
	sig = (int16)udata.u_argn;

	ptptr p;

	if (sig <= 0 || sig > 15)
		goto nogood;

	for (p = ptab; p < ptab + PTABSIZE; ++p) {
		if (p->p_pid == pid) {
			sendsig(p, sig);
			return (0);
		}
	}
nogood:
	udata.u_error = EINVAL;
	return (-1);
}

/********************************
alarm(uint16 secs)
*********************************/
int
_alarm(uint16 secs)
{
	secs = (int16)udata.u_argn;

	int retval;

	di();
	retval = udata.u_ptab->p_alarm;
	udata.u_ptab->p_alarm = secs;
	ei();
	return (retval);
}
