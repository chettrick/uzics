/**************************************************
UZI (Unix Z80 Implementation) Kernel:  config.h
***************************************************/


/* Remake devio.c when this is changed */
#ifdef DEVIO

extern wd_open(), wd_read(),wd_write();
extern fd_open(), fd_read(),fd_write();
extern tty_open(), tty_close(), tty_read(),tty_write();
extern lpr_open(), lpr_close(), lpr_write();
extern mem_read(),mem_write();
extern mt_read(), mt_write(), mt_open(), mt_close();
extern null_write();


static struct devsw dev_tab[] =  /* The device driver switch table */
{
    { 0x2b38, wd_open, ok,       wd_read, wd_write, nogood },
    { 0, fd_open, ok,       fd_read, fd_write, nogood },        /* floppy */
    { 0x3844, wd_open, ok,       wd_read, wd_write, nogood },
    { 0x252b, wd_open, ok,       wd_read, wd_write, nogood },   /* Swap */
    { 0, lpr_open, lpr_close, nogood, lpr_write, nogood},     /* printer */
    { 0, tty_open, tty_close, tty_read, tty_write, ok },      /* tty */
    { 0, ok, ok, ok, null_write, nogood },                      /* /dev/null */
    { 0, ok, ok, mem_read, mem_write, nogood },              /* /dev/mem */
    { 0, mt_open, mt_close, mt_read, mt_write, nogood }
};

#endif

#define NDEVS   3    /* Devices 0..NDEVS-1 are capable of being mounted */
#define TTYDEV  5    /* Device used by kernel for messages, panics */
#define SWAPDEV  3   /* Device for swapping. */
#define NBUFS  4     /* Number of block buffers */

