/**************************************************
UZI (Unix Z80 Implementation) Kernel:  machdep.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern int unix();	/* for doexec(). */

static int cursig;
static int (*curvec)();

/*
 * fs_init is called at the very beginning to initialize everything.
 * It is the equivalent of main().
 */
fs_init()
{
	di();
	stkreset();
	initvec();	/* Initialize the interrupt vector. */
	inint = 0;
	udata.u_insys = 1;
	out(0, 0xf1);	/* Turn off the clock. */
	ei();

	init2();	/* From process.c */
}

/*
 * valadr checks to see if a user-suppled address is legitimate.
 */
valadr(char *base, uint16 size)
{
	if (base < PROGBASE || base+size >= (char *)&udata) {
		udata.u_error = EFAULT;
		return (0);
	}
	return (1);
}

/*
 * addtick adds two tick counts together.  The t_time field holds
 * up to one second of ticks, while the t_date field counts minutes.
 */
addtick(time_t *t1, time_t *t2)
{
	t1->t_time += t2->t_time;
	t1->t_date += t2->t_date;
	if (t1->t_time >= 60 * TICKSPERSEC) {
		t1->t_time -= 60 * TICKSPERSEC;
		++t1->t_date;
	}
}

/*
 * incrtick counts seconds and minutes.
 */
incrtick(time_t *t)
{
	if (++t->t_time == 60 * TICKSPERSEC) {
		t->t_time = 0;
		++t->t_date;
	}
}

stkreset()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	POP	H
	LXI	SP,udata?-2
	PCHL
#endasm
#endif
}

tempstack()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	POP	H
	LXI	SP,100H
	PCHL
#endasm
#endif
}

initvec()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	LXI	H,vector?
	INX	H
	MOV	A,L
	ANI	0FEH
	MOV	L,A	;Set HL to first even address in vector[].
        MOV     A,H
.Z80
	LD	I,A	;Set interrupt register to upper 8 bits.
.8080
	MOV	A,L
	OUT	076H    ;Set external vector register with low order byte.
	LXI	D,service?
	MOV	M,E
	INX	H
	MOV	M,D	;Store address of service routine in vector[].
	RET
#endasm
#endif
}

doexec()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	POP	H
	POP	H	;Get argument.
	SPHL		;Set stack pointer to it.
	MVI	A,0C3H	;Jump inst.
	STA	0030H	;Dest of RST6 instruction.
	LXI	H,unix?	;Entry address.
	SHLD	0031H
	XRA	A
	STA	udata? + ?OSYS
	JMP	0100H
#endasm
#endif
}

/*
 * service is an interrupt device routine that calls the service
 * routine of each device that could have interrupted.
 */
service()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	PUSH	PSW
	PUSH	B
	PUSH	D
	PUSH	H
.Z80
	PUSH	IX
	PUSH	IY
.8080
#endasm
#endif

	inint = 1;

	if (tty_int())
		goto found;
	if (clk_int())
		goto found;
/* XXX - if (  ) ... */

	warning("Spurious interrupt");
found:
	inint = 0;

	/* Deal with a pending caught signal, if any. */
	if (!udata.u_insys)
		calltrap();
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
.Z80
	POP	IY
	POP	IX
.8080
	POP	H
	POP	D
	POP	B
	POP	PSW
	EI
	RET
#endasm
#endif
}

calltrap()
{
	/*
	 * Deal with a pending caught signal, if any.
	 * udata.u_insys should be false, and interrupts enabled.
	 * Remember, the user may never return from the trap routine.
	 */

	if (udata.u_cursig) {
		cursig = udata.u_cursig;
		curvec = udata.u_sigvec[cursig];
		udata.u_cursig = 0;
		/* Reset to default. */
		udata.u_sigvec[cursig] = SIG_DFL;
		ei();
		(*curvec)(cursig);
		di();
	} 
}

/*
 * Port addresses of clock chip registers.
 */
#define SECS 0xe2
#define MINS 0xe3
#define HRS 0xe4
#define DAY 0xe6
#define MON 0xe7
#define YEAR 86

sttime()
{
	panic("Calling sttime");
}

rdtime(time_t *tloc)
{
	di();
	tloc->t_time = tod.t_time;
	tloc->t_date = tod.t_date;
	ei();
}

/*
 * rdtod updates the global time of day.
 */
rdtod()
{
	tod.t_time = (tread(SECS) >> 1) | (tread(MINS) << 5)
	    | (tread(HRS) << 11);
	tod.t_date = tread(DAY) | (tread(MON) << 5) | (YEAR << 9);
}

/*
 * tread reads the BCD clock register and converts it to binary.
 */
tread(uint16 port)
{
	int n;

	n = in(port);
	return (10 * ((n >> 4) & 0x0f) + (n & 0x0f));
}

/*
 * di disables interrupts.
 */
di()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	DI	;Disable interrupts.
#endasm
#endif
}

/*
 * ei enables interrupts if its not in a service routine.
 */
ei()
{
	if (inint)
		return;
	;	/* XXX - Empty statement necessary to fool compiler. */
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	EI	;Enable interrupts.
#endasm
#endif
}

/*
 * shift8 shifts an unsigned int right 8 places.
 */
shift8()
{
#if 0	/* XXX - Comment out temporarily. */
#asm 8080
	POP	D	;Return address.
	POP	H
	MOV	L,H
	MVI	H,0
	MOV	A,L
	ANA	A	;Set Z flag on result.
	PUSH	H
	PUSH	D	;Restore stack.
#endasm
#endif
}

/*
 * panic prints an error message and dies.
 */
panic(char *s)
{
	di();
	inint = 1;
	kprintf("PANIC: %s\n", s);
	idump();
	abort();
}

/*
 * warning prints a warning message.
 */
warning(char *s)
{
	kprintf("WARNING: %s\n", s);
}

/*
 * puts sends the given string to stdout.
 */
puts(char *s)
{
	while (*s)
		kputchar(*(s++));
}

/* XXX - Some sort of crazy UNIX to DOS conversion :s */
kputchar(int c)
{
	if (c == '\n')
		_putc('\r');
	_putc(c);
	if (c == '\t')
		puts("\177\177\177\177\177\177\177\177\177\177");
}

idump()
{
	inoptr ip;
	ptptr pp;
	extern struct cinode i_tab[];

	kprintf("\tMAGIC\tDEV\tNUM\tMODE\tNLINK\t(DEV)\tREFS\tDIRTY err %d root %d\n",
	    udata.u_error, root - i_tab);

	for (ip = i_tab; ip < i_tab + ITABSIZE; ++ip) {
		kprintf("%d\t%d\t%d\t%u\t0%o\t%d\t%d\t%d\t%d\n",
		    ip - i_tab, ip->c_magic, ip->c_dev, ip->c_num,
		    ip->c_node.i_mode, ip->c_node.i_nlink,
		    ip->c_node.i_addr[0], ip->c_refs, ip->c_dirty);

		/****** XXX - Why is this commented out
		ifnot (ip->c_magic)     
			break;
		******/
	}

	kprintf("\n\tSTAT\tWAIT\tPID\tPPTR\tALARM\tPENDING\tIGNORED\n");
	for (pp = ptab; pp < ptab + PTABSIZE; ++pp) {
		kprintf("%d\t%d\t0x%x\t%d\t%d\t%d\t0x%x\t0x%x\n",
		    pp - ptab, pp->p_status, pp->p_wait,
		    pp->p_pid, pp->p_pptr-ptab, pp->p_alarm,
		    pp->p_pending, pp->p_ignored);
		ifnot (pp->p_pptr)
			break;
	}
    
	bufdump();

	kprintf("\ninsys %d ptab %d call %d cwd %d sp 0x%x\n",
	    udata.u_insys, udata.u_ptab - ptab, udata.u_callno,
	    udata.u_cwd - i_tab, udata.u_sp);
}

/*
 * kprintf is a short version of printf to save space.
 */
kprintf(nargs)
{
	register char **arg, *fmt;
	register c, base;
	char s[7], *itob();

	arg = (char **)&nargs + nargs;
	fmt = *arg;
	while (c = *fmt++) {
		if (c != '%') {
			kputchar(c);
			continue;
		}
		switch (c = *fmt++) {
		case 'c':
			kputchar(*--arg);
			continue;
		case 'd':
			base = -10;
			goto prt;
		case 'o':
			base = 8;
			goto prt;
		case 'u':
			base = 10;
			goto prt;
		case 'x':
			base = 16;
prt:
			puts(itob(*--arg, s, base));
			continue;
		case 's':
			puts(*--arg);
			continue;
		default:
			kputchar(c);
			continue;
		}
	}
}
