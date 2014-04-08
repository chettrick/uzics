/**************************************************
UZI (Unix Z80 Implementation) Kernel:  extern.h
***************************************************/

/* Global data structures */

#ifdef MAIN
#define extern  
#endif

extern struct u_data udata;	/* MUST BE FIRST */
extern struct p_tab ptab[PTABSIZE];

extern inoptr root;	/* Address of root dir in inode table. */
extern int16 ROOTDEV;	/* Device number of root filesystem. */

extern struct cinode i_tab[ITABSIZE];	/* In-core inode table. */
extern struct oft of_tab[OFTSIZE];	/* Open File Table. */

extern struct filesys fs_tab[NDEVS];	/* Table entry for each device with a filesystem. */
extern struct blkbuf bufpool[NBUFS];

extern ptptr initproc;	/* The process table address of the first process. */
extern int16 inint;	/* Flag is set whenever interrupts are being serviced. */

extern int16 sec;	/* Tick counter for counting off one second. */
extern int16 runticks;	/* Number of ticks current process has been swapped in. */

extern time_t tod;	/* Time of day. */
extern time_t ticks;	/* Cumulative tick counter, in minutes and ticks. */

extern char *swapbase;	/* Used by device driver for swapping. */
extern unsigned swapcnt;
extern blkno_t swapblk;

extern char vector[3];	/* Place for interrupt vector. */

#ifdef MAIN
#undef extern
#endif
