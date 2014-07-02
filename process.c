/**************************************************
UZI (Unix Z80 Implementation) Kernel:  process.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern void	bzero(void *, int);

void		init2(void);
void		psleep(char *);
void		wakeup(char *);
ptptr		getproc(void);
void		swapin(ptptr);
int		dofork(void);
int		clk_int(void);
int		unix(int16, int16, int16, int16, char *, int);
void		chksigs(void);
void		sendsig(ptptr, int16);
void		ssig(ptptr, int16);

static int	swapout(void);
static void	swrite(void);
static void	newproc(ptptr);
static ptptr	ptab_alloc(void);

extern int	(*disp_tab[])();

char *		stkptr;		/* Temp storage for swapout(). */
int16		newid;		/* Temp storage for dofork(). */

static int	j;		/* XXX - For unix()? */

void
init2(void)
{
	char *j;
	static char bootchar;
	static char *arg[2] = { "init", NULL };
	inoptr i_open(), n_open();
	ptptr ptab_alloc();

	bufinit();

	/* Create the context for the first process. */
	newproc(udata.u_ptab = initproc = ptab_alloc());
	initproc->p_status = P_RUNNING;

	/* User's file table. */
	for (j = udata.u_files; j < udata.u_files + UFTSIZE; ++j)
		*j = -1;

	/* Turn on the clock. */
	out(02, 0xf1);
	ei();

	/* Wait for an interrupted clock to set the time of day. */
	while (!tod.t_date)
		;

	/* Open the console tty device. */
	if (d_open(TTYDEV) != 0)
		panic("init2: no tty");

	kprintf("boot: ");
	udata.u_base = &bootchar;
	udata.u_count = 1;
	cdread(TTYDEV);
	ROOTDEV = bootchar - '0';

	/* Mount the root device. */
	if (fmount(ROOTDEV, NULLINODE))
		panic("init2: no file system");

	ifnot (root = i_open(ROOTDEV, ROOTINODE))
		panic("init2: no root");

	i_ref(udata.u_cwd = root);
	rdtime(&udata.u_time);

	udata.u_argn2 = (int16)("/init");
	udata.u_argn1 = (int16)(&arg[0]);
	udata.u_argn = (int16)(&arg[1]);
	_execve();
/* XXX - Why is this commented out
	_execve("/init",&arg[0],&arg[1]);
*/
	panic("init2: no /init");
}

/*
 * psleep puts a process to sleep on the given event.  If another
 * process is runnable, it swaps out the current one and starts the
 * new one.  Normally when psleep is called, the interrupts have
 * already been disabled.  An event of 0 means a pause(), while an
 * event equal to the process's own ptab address is a wait().
 */
void
psleep(char *event)
{
	int dummy;	/* Force saving of registers. */

	di();
	if (udata.u_ptab->p_status != P_RUNNING)
		panic("psleep: voodoo");
	if (!event)
		udata.u_ptab->p_status = P_PAUSE;
	else if (event == (char *)udata.u_ptab)
		udata.u_ptab->p_status = P_WAIT;
	else
		udata.u_ptab->p_status = P_SLEEP;

	udata.u_ptab->p_wait = event;

	ei();

	swapout();	/* Swap us out, and start another process. */
	/* swapout doesn't return until we have been swapped back in. */
}

/*
 * wakeup looks for any process waiting on the event and makes it runnable.
 */
void
wakeup(char *event)
{
	ptptr p;

	di();
	for (p = ptab; p < ptab + PTABSIZE; ++p) {
		if (p->p_status > P_RUNNING && p->p_wait == event) {
			p->p_status = P_READY;
			p->p_wait = (char *)NULL;
		}
	}
	ei();
}

/*
 * getproc returns the process table pointer of a runnable process.
 * It is actually the scheduler.  If there are none, it loops.
 * This is the only time-wasting loop in the system.
 */
ptptr
getproc(void)
{
	int status;
	static ptptr pp = ptab;	/* Pointer for round-robin scheduling. */

	for (;;) {
		if (++pp >= ptab + PTABSIZE)
		pp = ptab;

		di();
		status = pp->p_status;
		ei();

		if (status == P_RUNNING)
			panic("getproc: extra running");
		if (status == P_READY)
			return (pp);
	}
}

/*
 * swapout swaps out the current process, finds another that
 * is READY, possibly the same process, and swaps it in.
 * When a process is restarted after calling swapout,
 * it thinks it has just returned from swapout.
 * swapout can not have any arguments or auto variables.
 */
static int
swapout(void)
{
	static ptptr newp;
	ptptr getproc();

	/* See if any signals are pending. */
	chksigs();

	/* Get a new process. */
	newp = getproc();

	/* If there is nothing else to run, just return. */
	if (newp == udata.u_ptab) {
		udata.u_ptab->p_status = P_RUNNING;
		return (runticks = 0);
	}

	/* Save the stack pointer and critical registers. */
#if 0	/* XXX - Comment out temorarily. */
#asm
	LD	HL,01	;This will return 1 if swapped.
	PUSH	HL	;Will be the return value.
	PUSH	BC
	PUSH	IX
	LD	HL,0
	ADD	HL,SP	;Get SP into HL.
	LD	(stkptr?),HL
#endasm
#endif
	udata.u_sp = stkptr;

	swrite();
	/* Read the new process in, and return into its context. */
	swapin(newp);

	/* We should never get here. */
	panic("swapout: swapin failed");
}

/*
 * swrite actually writes out the image.
 */
static void
swrite(void)
{
	blkno_t blk;
	blk = udata.u_ptab->p_swap;

	/*
	 * Start by writing out the user data.
	 * The user data is written so that it
	 * is packed to the top of one block.
	 */
	swapwrite(SWAPDEV, blk, 512, ((char *)(&udata + 1)) - 512);

	/*
	 * The user address space is written in two i/o operations,
	 * one from 0x100 to the break, and then from the stack up.
	 * Notice that this might also include part or all of the
	 * user data, but never anything above it.
	 */
	swapwrite(SWAPDEV, blk + 1,
	    (((char *)(&udata + 1)) - PROGBASE) & ~511, PROGBASE);
}

/*
 * swapin swaps in the supplied process.
 */
void
swapin(ptptr pp)
{
	static blkno_t blk;
	static ptptr newp;

	di();
	newp = pp;
	blk = newp->p_swap;
	ei();

	/* No auto variables can be used past here. */
	tempstack();

	swapread(SWAPDEV, blk, 512, ((char *)(&udata + 1)) - 512);

	/*
	 * The user address space is read in two i/o operations,
	 * one from 0x100 to the break, and then from the stack up.
	 * Notice that this might also include part or all of the
	 * user data, but never anything above it.
	 */
	swapread(SWAPDEV, blk + 1,
	    (((char *)(&udata + 1)) - PROGBASE) & ~511, PROGBASE);

	if (newp != udata.u_ptab)
		panic("swapin: mangled swapin");
	di();
	newp->p_status = P_RUNNING;
	runticks = 0;
	ei();

	/* Restore the registers. */
	stkptr = udata.u_sp;
#if 0	/* XXX - Comment out temorarily. */
#asm
	LD	HL,(stkptr?)
	LD	SP,HL
	POP	IX
	POP	BC
	POP	HL
	LD	A,H
	OR	L
	RET		;Return into the context of the swapped-in process.
#endasm
#endif
}

/*
 * dofork implements forking.
 * dofork can not have any arguments or auto variables.
 */
int
dofork(void)
{
	static ptptr p;
	ptptr ptab_alloc();

	ifnot (p = ptab_alloc()) {
		udata.u_error = EAGAIN;
		return (-1);
	}
	di();
	udata.u_ptab->p_status = P_READY;	/* Parent is READY. */
	newid = p->p_pid;
	ei();

	/*
	 * Save the stack pointer and critical registers.
	 * When the process is swapped back in, it will be
	 * as if it returns with the value of the child's pid.
	 */
#if 0	/* XXX - Comment out temorarily. */
#asm
	LD	HL,(newid?)
	PUSH	HL
	PUSH	BC
	PUSH	IX
	LD	HL,0
	ADD	HL,SP	;Get SP into HL.
	LD	(stkptr?),HL
#endasm
#endif
	udata.u_sp = stkptr;
	swrite();
#if 0	/* XXX - Comment out temorarily. */
#asm
	POP	HL	;Repair stack pointer.
	POP	HL
	POP	HL
#endasm
#endif
	/* Make a new process table entry, etc. */
	newproc(p);

	di();
	runticks = 0;
	p->p_status = P_RUNNING;
	ei();
	return (0);	/* Return to child. */
}

/*
 * newproc fixes up the tables for the child of a fork.
 * p is a new process table entry.
 */
static void
newproc(ptptr p)
{
	char *j;

	/* Note that ptab_alloc clears most of the entry. */
	di();
	/* Allow 65 blocks per process. */
	p->p_swap = (p - ptab) * 65 + 1;
	p->p_status = P_RUNNING;
	p->p_pptr = udata.u_ptab;
	p->p_ignored = udata.u_ptab->p_ignored;
	p->p_uid = udata.u_ptab->p_uid;
	udata.u_ptab = p;
	/* Clear tick counters. */
	bzero(&udata.u_utime, 4 * sizeof(time_t));
	ei();

	rdtime(&udata.u_time);
	i_ref(udata.u_cwd);
	udata.u_cursig = udata.u_error = 0;

	for (j = udata.u_files; j < udata.u_files + UFTSIZE; ++j)
		if (*j >= 0)
			++of_tab[*j].o_refs;
}

/*
 * ptab_alloc allocates a new process table slot,
 * and fills in its p_pid field with a unique number.
 */
static ptptr
ptab_alloc(void)
{
	ptptr p;
	ptptr pp;
	static int nextpid = 0;

	di();
	for (p = ptab; p < ptab + PTABSIZE; ++p)
		if (p->p_status == P_EMPTY)
			goto found;
	ei();
	return (NULL);
found:
nogood:
	/* See if next pid number is unique. */
	if (nextpid++ > 32000)
		nextpid = 1;
	for (pp = ptab; pp < ptab + PTABSIZE; ++pp)
		if (pp->p_status != P_EMPTY && pp->p_pid == nextpid)
			goto nogood;

	bzero(p, sizeof(struct p_tab));
	p->p_pid = nextpid;
	p->p_status = P_FORKING;
	ei();
	return (p);
}

/*
 * clk_int is the clock interrupt routine.  Its job is to increment
 * the clock counters, increment the tick count of the running
 * process, and either swap it out if it has been in long enough and
 * is in user space, or mark it to be swapped out if in system space.
 * Also it decrements the alarm clock of processes.
 * clk_int can not have any auto or register variables.
 */
int
clk_int(void)
{
	static ptptr p;

	/* See if clock actually interrupted, and turn it off. */
	ifnot (in(0xf0))
		return (0);

	/* Increment processes and global tick counters. */
	if (udata.u_ptab->p_status == P_RUNNING)
		incrtick(udata.u_insys ? &udata.u_stime : &udata.u_utime);

	incrtick(&ticks);

	/* Do once-per-second things. */
	if (++sec == TICKSPERSEC) {
		sec = 0;	/* Update global time counters. */
		rdtod();	/* Update time-of-day. */

		/* Update process alarm clocks. */
		for (p = ptab; p < ptab + PTABSIZE; ++p)
			if (p->p_alarm)
				ifnot (--p->p_alarm)
					sendsig(p, SIGALRM);
	}

	/* Check run time of current process. */
	if (++runticks >= MAXTICKS && !udata.u_insys) {
		/* Time to swap out. */
		udata.u_insys = 1;
		inint = 0;
		udata.u_ptab->p_status = P_READY;
		swapout();
		di();
		udata.u_insys = 0;	/* We have swapped back in. */
	}
	return (1);
}

/*
 * No auto variables here, so carry flag will be preserved.
 */
int
unix(int16 argn3, int16 argn2, int16 argn1, int16 argn, char *uret, int callno)
{
	udata.u_argn3 = argn3;
	udata.u_argn2 = argn2;
	udata.u_argn1 = argn1;
	udata.u_argn = argn;
	udata.u_retloc = uret;
	udata.u_callno = callno;

	udata.u_insys = 1;
	udata.u_error = 0;
	ei();

#ifdef DEBUG
	kprintf("\t\t\t\t\tcall %d (%x, %x, %x)\n",
	    callno, argn2, argn1, argn);
#endif

	/* Branch to correct routine. */
	udata.u_retval = (*disp_tab[udata.u_callno])();

#ifdef DEBUG
	kprintf("\t\t\t\t\t\tcall %d ret %x err %d\n",
	    udata.u_callno, udata.u_retval, udata.u_error);
#endif

	chksigs();
	di();
	if (runticks >= MAXTICKS) {
		udata.u_ptab->p_status = P_READY;
		swapout();
	}
	ei();

	udata.u_insys = 0;
	calltrap();	/* Call trap routine if necessary. */

	/* If an error, return errno with carry set. */
	if (udata.u_error) {
	;
#if 0	/* XXX - Comment out temorarily. */
#asm
	LD	HL,(udata? + ?OERR)
	POP	BC	;Restore frame pointer.
	PUSH	BC
	POP	IX
	SCF
	RET
#endasm
#endif
	;
	}
	return (udata.u_retval);
}

/*
 * chksigs sees if the current process has any signals set, and deals with them.
 */
void
chksigs(void)
{
	int j;

	di();
	ifnot (udata.u_ptab->p_pending) {
		ei();
		return;
	}

	for (j = 1; j < NSIGS; ++j) {
		ifnot (sigmask(j) & udata.u_ptab->p_pending)
			continue;
		if (udata.u_sigvec[j] == SIG_DFL) {
			ei();
			doexit(0, j);
		}
		if (udata.u_sigvec[j] != SIG_IGN) {
			/* Arrange to call the user routine at return. */
			udata.u_ptab->p_pending &= !sigmask(j);
			udata.u_cursig = j;
		}
	}
	ei();
}

void
sendsig(ptptr proc, int16 sig)
{
	ptptr p;

	if (proc)
		ssig(proc, sig);
	else
		for (p = ptab; p < ptab + PTABSIZE; ++p)
			if (p->p_status)
				ssig(p, sig);
}

void
ssig(ptptr proc, int16 sig)
{
	char stat;

	di();
	ifnot (proc->p_status)
		goto done;	/* Presumably was killed just now. */

	if (proc->p_ignored & sigmask(sig))
		goto done;

	stat = proc->p_status;
	if (stat == P_PAUSE || stat == P_WAIT || stat == P_SLEEP)
		proc->p_status = P_READY;

	proc->p_wait = (char *)NULL;
	proc->p_pending |= sigmask(sig);
done:
	ei();
}
