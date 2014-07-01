/**************************************************
UZI (Unix Z80 Implementation) Kernel:  config.h
***************************************************/

extern void		bcopy(const void *, void *, int);
extern void		bzero(void *, int);
extern void		abort(void);

extern void		di(void);
extern void		ei(void);

/* Remake devio.c when this is changed. */
#ifdef DEVIO
/* devwd.c */
extern int		wd_open(int);
extern char		wd_read(unsigned int, int);
extern char		wd_write(unsigned int, int);

/* devfd.c */
extern int		fd_open(int);
extern unsigned int	fd_read(int16, int);
extern unsigned int	fd_write(int16, int);

/* devtty.c */
extern int		tty_open(int);
extern int		tty_close(int);
extern int		tty_read(int16, int16);
extern int		tty_write(int16, int16);
extern int		tty_int(void);	/* XXX - Used by machdep.c. */
extern void		_putc(char);	/* XXX - Used by machdep.c. */

/* devmisc.c */
extern int		lpr_open(void);
extern int		lpr_close(void);
extern unsigned int	lpr_write(int, int);
extern unsigned int	mem_read(int, int);
extern unsigned int	mem_write(int, int);
extern unsigned int	null_write(int, int);

/* The device driver switch table */
static struct devsw dev_tab[] = {
	{ 0x2b38, wd_open, ok, wd_read, wd_write, nogood },
	{ 0, fd_open, ok, fd_read, fd_write, nogood },		/* floppy */
	{ 0x3844, wd_open, ok, wd_read, wd_write, nogood },
	{ 0x252b, wd_open, ok, wd_read, wd_write, nogood },	/* swap */
	{ 0, lpr_open, lpr_close, nogood, lpr_write, nogood },	/* printer */
	{ 0, tty_open, tty_close, tty_read, tty_write, ok },	/* tty */
	{ 0, ok, ok, ok, null_write, nogood },			/* /dev/null */
	{ 0, ok, ok, mem_read, mem_write, nogood },		/* /dev/mem */
};
#endif

#define NBUFS	4	/* Number of block buffers. */
#define NDEVS	3	/* Devices 0..NDEVS-1 are capable of being mounted. */
#define SWAPDEV	3	/* Device for swapping. */
#define TTYDEV	5	/* Device used by kernel for messages and panics. */
