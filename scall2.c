/**************************************************
UZI (Unix Z80 Implementation) Kernel:  scall2.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern void	bcopy(void *, void *, int);
extern void	bzero(void *, int);

/* Getpid() */
_getpid()
{
	return (udata.u_ptab->p_pid);
}

/* Getppid() */
_getppid()
{
	return (udata.u_ptab->p_pptr->p_pid);
}

/* Getuid() */
_getuid()
{
	return (udata.u_ptab->p_uid);
}

_getgid()
{
	return (udata.u_gid);
}

/*********************************
setuid(int uid)
***********************************/

#define uid (int)udata.u_argn

_setuid()
{
	if (super() || udata.u_ptab->p_uid == uid) {
		udata.u_ptab->p_uid = uid;
		udata.u_euid = uid;
		return (0);
	}
	udata.u_error = EPERM;
	return (-1);
}

#undef uid

/*****************************************
setgid(int gid)
****************************************/

#define gid (int16)udata.u_argn

_setgid()
{
	if (super() || udata.u_gid == gid) {
		udata.u_gid = gid;
		udata.u_egid = gid;
		return (0);
	}
	udata.u_error = EPERM;
	return (-1);
}

#undef gid

/***********************************
time(int tvec[])
**************************************/

#define tvec (int *)udata.u_argn

_time()
{
	rdtime(tvec);	/* In machdep.c */
	return (0);
}

#undef tvec

/**************************************
stime(int tvec[])
**********************************/

#define tvec (int *)udata.u_argn

_stime()
{
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

#undef tvec

/********************************************
times(char *buf)
**********************************************/

#define buf (char *)udata.u_argn

_times()
{
	ifnot (valadr(buf, 6 * sizeof(time_t)))
		return (-1);

	di();
	bcopy(&udata.u_utime, buf, 4 * sizeof(time_t));
	bcopy(&ticks, buf + 4 * sizeof(time_t), sizeof(time_t));
	ei();
	return (0);
}

#undef buf

/* User's execve() call. All other flavors are library routines. */

/*****************************************
execve(char *name, char *argv[], char *envp[])
*****************************************/

#define name (char *)udata.u_argn2
#define argv (char **)udata.u_argn1
#define envp (char **)udata.u_argn

_execve()
{
	register inoptr ino;
	register char *buf;
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

	tempstk();	/* XXX - Where is this defined? */
	exec2();	/* Never returns. */
nogood2:
	bfree(buf, 0);
nogood:
	i_deref(ino);
	return (-1);
}

#undef name
#undef argv
#undef envp

exec2()
{
	register blkno_t blk;
	register char **argv;
	register char **envp;
	register int (**sp)();
	int argc;
	char *rargs();
	register char *progptr;
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

wargs(char **argv, int blk)
{
	/* Address of base of arg strings in user space. */
	register char *ptr;
	register int n;
	struct s_argblk *argbuf;
	register char *bufp;
	register int j;
	char *zerobuf();
	char *bread();

	/* Gather the arguments and put them on the swap device. */
	argbuf = (struct s_argblk *)bread(SWAPDEV, udata.u_ptab->p_swap + blk, 2);
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

char *
rargs(register char *ptr, int blk, int *cnt)
{
	struct s_argblk *argbuf;
	register char **argv;	/* Address of users argv[], just below ptr. */
	register int n;
	char *bread();

	/* Read back the arguments. */
	argbuf = (struct s_argblk *)bread(SWAPDEV, udata.u_ptab->p_swap + blk, 0);

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

#define addr (char *)udata.u_argn

_brk()
{
	char dummy;	/* A thing to take address of. */

	/* A hack to get approx val of stack ptr. */
	if (addr < PROGBASE || (addr + 64) >= (char *)&dummy) {
		udata.u_error = ENOMEM;
		return (-1);
	}
	udata.u_break = addr;
	return (0);
}

#undef addr

/************************************
sbrk(uint16 incr)
***************************************/

#define incr (uint16)udata.u_argn

_sbrk()
{
	register char *oldbrk;

	udata.u_argn += (oldbrk = udata.u_break);
	if (_brk())
		return (-1);

	return ((int)oldbrk);
}

#undef incr

/**************************************
wait(int *statloc)
****************************************/

#define statloc (int *)udata.u_argn

_wait()
{
	register ptptr p;
	register int retval;

	if (statloc > (int *)(&udata)) {
		udata.u_error = EFAULT;
		return (-1);
	}

	di();
	/* See if we have any children. */
	for (p = ptab; p < ptab + PTABSIZE; ++p)
		if (p->p_status && p->p_pptr == udata.u_ptab && p != udata.u_ptab)
			goto ok;
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

#undef statloc

/**************************************
_exit(int16 val)
**************************************/

#define val (int16)udata.u_argn

__exit()
{
	doexit(val, 0);
}

#undef val

doexit(int16 val, int16 val2)
{
	register int16 j;
	register ptptr p;

	for (j = 0; j < UFTSIZE; ++j)
		ifnot (udata.u_files[j] & 0x80)	/* Portable equivalent of == -1. */
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

_fork()
{
	return (dofork());
}

_pause()
{
	psleep(0);
	udata.u_error = EINTR;
	return (-1);
}

/*************************************
signal(int16 sig, int16 (*func)())
***************************************/

#define sig (int16)udata.u_argn1
#define func (int (*)())udata.u_argn

_signal()
{
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

#undef sig
#undef func

/**************************************
kill(int16 pid, int16 sig)
*****************************************/

#define pid (int16)udata.u_argn1
#define sig (int16)udata.u_argn

_kill()
{
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

#undef pid
#undef sig

/********************************
alarm(uint16 secs)
*********************************/

#define secs (int16)udata.u_argn

_alarm()
{
	int retval;

	di();
	retval = udata.u_ptab->p_alarm;
	udata.u_ptab->p_alarm = secs;
	ei();
	return (retval);
}

#undef secs
