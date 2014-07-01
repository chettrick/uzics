/**************************************************
UZI (Unix Z80 Implementation) Kernel:  devtty.c
***************************************************/

#include "unix.h"

#define TTYSIZ 132

char ttyinbuf[TTYSIZ];

extern struct u_data udata;

struct s_queue ttyinq = {
	ttyinbuf,
	ttyinbuf,
	ttyinbuf,
	TTYSIZ,
	0,
	TTYSIZ / 2
};

int stopflag;	/* Flag for ^S/^Q */
int flshflag;	/* Flag for ^O */

int		tty_open(int);
int		tty_close(int);
int		tty_read(int16, int16);
int		tty_write(int16, int16);
int		tty_int(void);		/* XXX - Used by machdep.c. */
void		_putc(char);		/* XXX - Used by machdep.c. */

static int	tty_ioctl(int);

int
tty_open(int minor)
{
	return (0);
}

int
tty_close(int minor)
{
	return (0);
}

int
tty_read(int16 minor, int16 rawflag)
{
	int nread;

	nread = 0;
	while (nread < udata.u_count) {
		for (;;) {
			di();
			if (remq(&ttyinq, udata.u_base))
				break;
			psleep(&ttyinq);
			/* XXX - messy */
			if (udata.u_cursig || udata.u_ptab->p_pending) {
				udata.u_error = EINTR;
				return (-1);
			}
		}
		ei();   
		if ((nread++ == 0) && (*udata.u_base == '\004'))	/* ^D */
			return (0);
		if (*udata.u_base == '\n')
			break;
		++udata.u_base;
	} 
	return (nread);
}

int
tty_write(int16 minor, int16 rawflag)
{
	int towrite;

	towrite = udata.u_count;

	while (udata.u_count-- != 0) {
		/* Wait on the ^S/^Q flag. */
		for (;;) {
			di();
			ifnot (stopflag)    
				break;
			psleep(&stopflag);
			/* XXX - messy */
			if (udata.u_cursig || udata.u_ptab->p_pending) {
				udata.u_error = EINTR;
				return (-1);
			}
		}
		ei();   
		ifnot (flshflag) {
			if (*udata.u_base == '\n')
				_putc('\r');
			_putc(*udata.u_base);
		}
		++udata.u_base;
	}
	return (towrite);
}

static int
tty_ioctl(int minor)
{
	return (-1);
}

/*
 * This tty interrupt routine checks to see if the uart receiver
 * actually caused the interrupt.  If so it adds the character to
 * the tty input queue, echoing and processing backspace and carriage
 * return.  If the queue contains a full line, it wakes up anything
 * waiting on it.  If it is totally full, it beeps at the user.
 */
int
tty_int(void)
{
	char c;
	int found;
	char oc;

	found = 0;
again:
	if ((in(0x72) & 0x81) != 0x81)
		return (found);
	c = (in(0x73) & 0x7f);

	if (c == 0x1a)			/* ^Z */
		idump();		/* For debugging. */

	if (c == '\003')		/* ^C */
		sendsig(NULL, SIGINT);
	else if (c == '\017')		/* ^O */
		flshflag = !flshflag;
	else if (c == '\023')		/* ^S */
		stopflag = 1;
	else if (c == '\021') {		/* ^Q */
		stopflag = 0;
		wakeup(&stopflag);
	} else if (c == '\b') {
		if (uninsq(&ttyinq, &oc)) {
			if (oc == '\n') {
				/* Don't erase past newline. */
				insq(&ttyinq, oc);
			} else {
				_putc('\b');
				_putc(' ');
				_putc('\b');
			}
		}
	} else {
		if ((c == '\r') || (c == '\n')) {
			c = '\n';
			_putc('\r');
		}

		if (insq(&ttyinq, c))
			_putc(c);
		else
			_putc('\007');	/* Beep if no more room. */
	}

	if ((c == '\n') || (c == '\004'))	/* ^D */
		wakeup(&ttyinq);

	found = 1;
	goto again;	/* Loop until the uart has no data ready. */
}

/* XXX - Remove vax specific code */
#ifdef vax
void
_putc(char c)
{
	write(1, &c, 1);
}
#else
void
_putc(char c)
{
	while (!(in(0x72) & 02))
		;
	out(c, 0x73);
}
#endif
