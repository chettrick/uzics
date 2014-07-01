/**************************************************
UZI (Unix Z80 Implementation) Kernel:  devio.c
***************************************************/

#define DEVIO

static int	ok(void);
static int	nogood(void);

#include "unix.h"
#include "extern.h"

char *		bread(int, blkno_t, int);
void		brelse(bufptr);
void		bawrite(bufptr);
int		bfree(bufptr, int);
char *		zerobuf(void);
void		bufsync(void);
void		bufdump(void);
int		cdread(int);
int		cdwrite(int);
int		swapread(int, blkno_t, unsigned int, char *);
int		swapwrite(int, blkno_t, unsigned int, char *);
int		d_open(int);
int		d_close(int);
int		d_ioctl(int, int, char *);
int		validdev(int);
int		insq(struct s_queue *, char);
int		remq(struct s_queue *, char *);
int		uninsq(struct s_queue *, char *);
int		fullq(struct s_queue *);

static bufptr	bfind(int, blkno_t);
static bufptr	freebuf(void);
static void	bufinit(void);
static int	bdread(bufptr);
static int	bdwrite(bufptr);

/* Buffer pool management. */

/********************************************************
The high-level interface is through bread() and bfree().
bread() is given a device and block number, and a rewrite flag.
If rewrite is 0, the block is actually read if it is not already
in the buffer pool. If rewrite is set, it is assumed that the caller
plans to rewrite the entire contents of the block, and it will
not be read in, but only have a buffer named after it.

bfree() is given a buffer pointer and a dirty flag.
If the dirty flag is 0, the buffer is made available for further 
use.  If the flag is 1, the buffer is marked "dirty", and
it will eventually be written out to disk.  If the flag is 2,
it will be immediately written out.

zerobuf() returns a buffer of zeroes not belonging to any
device.  It must be bfree'd after use, and must not be
dirty. It is used when a read() wants to read an unallocated
block of a file.

bufsync() write outs all dirty blocks.

XXX - Note that a pointer to a buffer structure is the
same as a pointer to the data.  This is very important.

********************************************************/

unsigned bufclock = 0;	/* Time-stamp counter for LRU. */

char *
bread(int dev, blkno_t blk, int rewrite)
{
	bufptr bp;
	bufptr bfind();
	bufptr freebuf();

	if (bp = bfind(dev, blk)) {
		if (bp->bf_busy)
			panic("want busy block");
		goto done;
	}
	bp = freebuf();

	bp->bf_dev = dev;
	bp->bf_blk = blk;

	/*
	 * If rewrite is set, we are about to write over the
	 * entire block, so we don't need the previous contents.
	 */
	ifnot (rewrite)
		if (bdread(bp) == -1) {
			udata.u_error = EIO;
			return (NULL);
		}

	if (rewrite == 2)
		bzero(bp->bf_data, 512);
done:
	bp->bf_busy = 1;
	bp->bf_time = ++bufclock;	/* Time stamp it. */
	return (bp->bf_data);
}

void
brelse(bufptr bp)
{
	bfree(bp, 0);
}

void
bawrite(bufptr bp)
{
	bfree(bp, 1);
}

int
bfree(bufptr bp, int dirty)
{
	bp->bf_dirty |= dirty;
	bp->bf_busy = 0;

	if (dirty == 2) {	/* Extra dirty. */
		if (bdwrite(bp) == -1)
			udata.u_error = EIO;
		bp->bf_dirty = 0;
		return (-1);
	}
	return (0);
}

char *
zerobuf(void)
{
	bufptr bp;
	bufptr freebuf();

	bp = freebuf();
	bp->bf_dev = -1;
	bzero(bp->bf_data, 512);
	return (bp->bf_data);
}

void
bufsync(void)
{
	bufptr bp;

	for (bp = bufpool; bp < bufpool + NBUFS; ++bp)
		if (bp->bf_dev != -1 && bp->bf_dirty)
			bdwrite(bp);
}

static bufptr
bfind(int dev, blkno_t blk)
{
	bufptr bp;

	for (bp = bufpool; bp < bufpool + NBUFS; ++bp)
		if (bp->bf_dev == dev && bp->bf_blk == blk)
			return (bp);
	return (NULL);
}

static bufptr
freebuf(void)
{
	bufptr bp;
	bufptr oldest;
	int oldtime;

	/*
	 * Try to find a non-busy buffer and
	 * write out the data if it is dirty.
	 */
	oldest = NULL;
	oldtime = 0;
	for (bp = bufpool; bp < bufpool + NBUFS; ++bp) {
		if (bufclock - bp->bf_time >= oldtime && !bp->bf_busy) {
			oldest = bp;
			oldtime = bufclock - bp->bf_time;
		}
	}

	ifnot (oldest)
		panic("no free buffers");
    
	if (oldest->bf_dirty) {
		if (bdwrite(oldest) == -1)
			udata.u_error = EIO;
		oldest->bf_dirty = 0;
	}
	return (oldest);
}

static void
bufinit(void)
{
	bufptr bp;

	for (bp = bufpool; bp < bufpool + NBUFS; ++bp)
		bp->bf_dev = -1;
}

void
bufdump(void)
{
	bufptr j;

	kprintf("\ndev\tblock\tdirty\tbusy\ttime clock %d\n", bufclock);
	for (j = bufpool; j < bufpool + NBUFS; ++j)
		kprintf("%d\t%u\t%d\t%d\t%u\n", j->bf_dev, j->bf_blk,
		    j->bf_dirty, j->bf_busy, j->bf_time);
}

/***************************************************
bdread() and bdwrite() are the block device interface routines.
they are given a buffer pointer, which contains the device,
block number, and data location.
They basically validate the device and vector the call.

cdread() and cdwrite() are the same for character (or "raw")
devices, and are handed a device number.
udata.u_base, count, and offset have the rest of the data.
****************************************************/

static int
bdread(bufptr bp)
{
	ifnot (validdev(bp->bf_dev))
		panic("bdread: invalid dev");
	udata.u_buf = bp;
	return ((*dev_tab[bp->bf_dev].dev_read)(dev_tab[bp->bf_dev].minor, 0));
}

static int
bdwrite(bufptr bp)
{
	ifnot (validdev(bp->bf_dev))
		panic("bdwrite: invalid dev");
	udata.u_buf = bp;
	return ((*dev_tab[bp->bf_dev].dev_write)(dev_tab[bp->bf_dev].minor, 0));
}

int
cdread(int dev)
{
	ifnot (validdev(dev))
		panic("cdread: invalid dev");
	return ((*dev_tab[dev].dev_read)(dev_tab[dev].minor, 1));
}

int
cdwrite(int dev)
{
	ifnot (validdev(dev))
		panic("cdwrite: invalid dev");
	return ((*dev_tab[dev].dev_write)(dev_tab[dev].minor, 1));
}

int
swapread(int dev, blkno_t blkno, unsigned int nbytes, char *buf)
{
	swapbase = buf;
	swapcnt = nbytes;
	swapblk = blkno;
	return ((*dev_tab[dev].dev_read)(dev_tab[dev].minor, 2));
}

int
swapwrite(int dev, blkno_t blkno, unsigned int nbytes, char *buf)
{
	swapbase = buf;
	swapcnt = nbytes;
	swapblk = blkno;
	return ((*dev_tab[dev].dev_write)(dev_tab[dev].minor, 2));
}

/**************************************************
The device driver read and write routines now have
only two arguments, minor and rawflag.  If rawflag is
zero, a single block is desired, and the necessary data
can be found in udata.u_buf.
Otherwise, a "raw" or character read is desired, and
udata.u_offset, udata.u_count, and udata.u_base
should be consulted instead.
Any device other than a disk will have only raw access.
*****************************************************/

int
d_open(int dev)
{
	ifnot (validdev(dev))
		return (-1);
	return ((*dev_tab[dev].dev_open)(dev_tab[dev].minor));
}

int
d_close(int dev)
{
	ifnot (validdev(dev))
		panic("d_close: bad device");
	return ((*dev_tab[dev].dev_close)(dev_tab[dev].minor));
}

int
d_ioctl(int dev, int request, char *data)
{
	ifnot (validdev(dev)) {
		udata.u_error = ENXIO;
		return (-1);
	}
	if ((*dev_tab[dev].dev_ioctl)(dev_tab[dev].minor,
	    request, data)) {
		udata.u_error = EINVAL;
		return (-1);
	}
	return (0);
}

static int
ok(void)
{
	return (0);
}

static int
nogood(void)
{
	return (-1);
}

int
validdev(int dev)
{
	return (dev >= 0 && dev < (sizeof(dev_tab) / sizeof(struct devsw)));
}

/*************************************************************
Character queue management routines
************************************************************/

/* Add something to the tail. */
int
insq(struct s_queue *q, char c)
{
	di();
	if (q->q_count == q->q_size) {
		ei();
		return (0);
	}
	*(q->q_tail) = c;
	++q->q_count;
	if (++q->q_tail >= q->q_base + q->q_size)
		q->q_tail = q->q_base;
	ei();
	return (1);
}

/* Remove something from the head. */
int
remq(struct s_queue *q, char *cp)
{
	di();
	ifnot (q->q_count) {
		ei();
		return (0);
	}
	*cp = *(q->q_head);
	--q->q_count;
	if (++q->q_head >= q->q_base + q->q_size)
		q->q_head = q->q_base;
	ei();
	return (1);
}

/* Remove something from the tail; the most recently added char. */
int
uninsq(struct s_queue *q, char *cp)
{
	di();
	ifnot (q->q_count) {
		ei();
		return (0);
	}
	--q->q_count;
	if (--q->q_tail <= q->q_base)
		q->q_tail = q->q_base + q->q_size - 1;
	*cp = *(q->q_tail);
	ei();
	return (1);
}

/* Returns true if the queue has more characters than its wakeup number. */
int
fullq(struct s_queue *q)
{
	di();
	if (q->q_count > q->q_wakeup) {
		ei();
		return (1);
	}
	ei();
	return (0);
}
