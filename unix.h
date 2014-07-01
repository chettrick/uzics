/**************************************************
UZI (Unix Z80 Implementation) Kernel:  unix.h
***************************************************/

#ifndef vax
#define CPM
#endif

#define UFTSIZE		10	/* Number of user files. */
#define OFTSIZE		15	/* Open file table size. */
#define ITABSIZE	20	/* Inode table size. */
#define PTABSIZE	20	/* Process table size. */

#define NSIGS		16	/* Number of signals <= 16. */

#define ROOTINODE	1	/* Root inode for all mounted filesystems. */

#define TICKSPERSEC	10	/* Ticks per second. */
#define MAXTICKS	10	/* Max ticks before swap out (time slice). */

#define ARGBLK		0	/* Block num on SWAPDEV for arguments. */
#define PROGBASE	((char *)(0x100))
#define MAXEXEC		0	/* Max num of blocks of executable file. */

#define EMAGIC		0xc3	/* Header of executable. */
#define CMAGIC		24721	/* Random num for cinode c_magic. */
#define SMOUNTED	12742	/* Magic num to specify mounted filesystem. */
#define NULL		((void *)0)

/* XXX - Macros to trick the compiler into generating more compact code. */
#define ifnull(e)	if(e){}else
#define ifnot(e)	if(e){}else
#define ifzero(e)	if(e){}else

#ifdef CPM
	typedef	unsigned uint16;
	typedef	int int16;
#else
	typedef	unsigned short uint16;
	typedef	short int16;
#endif

typedef struct s_queue {
	char	*q_base;	/* Pointer to data. */
	char	*q_head;	/* Pointer to address of next char to read. */
	char	*q_tail;	/* Pointer to where next char to insert goes. */
	int	q_size;		/* Max size of queue. */
	int	q_count;	/* How many characters presently in queue. */
	int	q_wakeup;	/* Threshold for waking up procs waiting on queue. */
} queue_t;

typedef struct time_s {
	uint16	t_time;
	uint16	t_date;
} time_t;

/* User's structure for times() system call. */
struct tms {
	time_t	tms_utime;
	time_t	tms_stime;
	time_t	tms_cutime;
	time_t	tms_cstime;
	time_t	tms_etime;	/* Elapsed real time. */
} ;

/* Flags for setftime(). */
#define A_TIME		1
#define M_TIME		2
#define C_TIME		4

typedef struct off_t {
	uint16	o_blkno;	/* Block number. */
	int16	o_offset;	/* Offset within block 0 - 511. */
} off_t;

typedef	uint16 blkno_t;		/* Can have 65536 512-byte blocks in filesystem. */
#define NULLBLK		((blkno_t) - 1)

typedef struct blkbuf {
	char	bf_data[512];	/* XXX - This MUST be first! */
	char	bf_dev;
	blkno_t	bf_blk;
	char	bf_dirty;
	char	bf_busy;
	uint16	bf_time;	/* LRU time stamp. */
	struct	blkbuf *bf_next; /* LRU free list pointer. */
} blkbuf, *bufptr;

typedef struct dinode {
	uint16	i_mode;
	uint16	i_nlink;
	uint16	i_uid;
	uint16	i_gid;
	off_t	i_size;
	time_t	i_atime;
	time_t	i_mtime;
	time_t	i_ctime;
	blkno_t	i_addr[20];
} dinode;			/* XXX - Exactly 64 bytes long! */

/* Really only used by users. */
struct stat {
	int16	st_dev;
	uint16	st_ino;
	uint16	st_mode;
	uint16	st_nlink;
	uint16	st_uid;
	uint16	st_gid;
	uint16	st_rdev;
	off_t	st_size;
	time_t	st_atime;
	time_t	st_mtime;
	time_t	st_ctime;
};

/* Bit masks for i_mode and st_mode. */
#define OTH_EX		0001
#define OTH_WR		0002
#define OTH_RD		0004
#define GRP_EX		0010
#define GRP_WR		0020
#define GRP_RD		0040
#define OWN_EX		0100
#define OWN_WR		0200
#define OWN_RD		0400

#define SAV_TXT		01000
#define SET_GID		02000
#define SET_UID		04000

#define MODE_MASK	07777

#define F_REG		0100000
#define F_DIR		040000
#define F_PIPE		010000
#define F_BDEV		060000
#define F_CDEV		020000

#define F_MASK		0170000

typedef struct cinode {
	int	c_magic;	/* Used to check for corruption. */
	int	c_dev;		/* Inode's device. */
	unsigned c_num;		/* Inode number. */
	dinode	c_node;
	char	c_refs;		/* In-core reference count. */
	char	c_dirty;	/* Modified flag. */
} cinode, *inoptr;

#define NULLINODE	((inoptr)NULL)
#define NULLINOPTR	((inoptr*)NULL)

typedef struct direct {
	uint16	d_ino;
	char	d_name[14];
} direct;

typedef struct filesys {
	int16	s_mounted;
	uint16	s_isize;
	uint16	s_fsize;
	int16	s_nfree;
	blkno_t	s_free[50];
	int16	s_ninode;
	uint16	s_inode[50];
	int16	s_fmod;
	time_t	s_time;
	blkno_t	s_tfree;
	uint16	s_tinode;
	inoptr	s_mntpt;	/* Mount point. */
} filesys, *fsptr;

typedef struct oft {
	off_t	o_ptr;		/* File position pointer. */
	inoptr	o_inode;	/* Pointer into in-core inode table. */
	char	o_access;	/* O_RDONLY, O_WRONLY, or O_RDWR. */
	char	o_refs;		/* Reference count: depends on num of active children. */
} oft;

/* Process table p_status values. */
#define P_EMPTY		0	/* Unused slot. */
#define P_RUNNING	1	/* Currently running process. */
#define P_READY		2	/* Runnable. */
#define P_SLEEP		3	/* Sleeping; can be awakened by signal. */
#define P_XSLEEP	4	/* Sleeping; don't wake up for signal. */
#define P_PAUSE		5	/* Sleeping for pause(); can wakeup for signal. */
#define P_FORKING	6	/* In process of forking; do not mess with. */
#define P_WAIT		7	/* Executed a wait(). */
#define P_ZOMBIE	8	/* Exited. */

#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGIOT		6
#define SIGEMT		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15

#define SIG_DFL		(int (*)())0
#define SIG_IGN		(int (*)())1

#define sigmask(sig)	(1 << (sig))

/* Process table entry. */
typedef struct p_tab {
	char	p_status;	/* Process status. */
	int	p_pid;		/* Process ID. */
	int	p_uid;
	struct	p_tab *p_pptr;	/* Process parent's table entry. */
	blkno_t	p_swap;		/* Starting block of swap space. */
	unsigned p_alarm;	/* Seconds until alarm goes off. */
	unsigned p_exitval;	/* Exit value. */
	/* Everything below here is overlaid by time info at exit. */
	char	*p_wait;	/* Address of thing waited for. */
	int	p_priority;	/* Process priority. */
	uint16	p_pending;	/* Pending signals. */
	uint16	p_ignored;	/* Ignored signals. */
} p_tab, *ptptr;

/* Per-process data (swapped with process). */
#if 0	/* XXX - Comment out temporarily. */
__asm 8080
?OSYS	equ	2		;Byte offsets of elements of u_data.
?OCALL	equ	3
?ORET	equ	4		;Return location.
?ORVAL	equ	6		;Return value.
?OERR	equ	8		;Error number.
?OSP	equ	10		;Users stack pointer.
?OBC	equ	12		;Users frame pointer.
__endasm;
#endif

typedef struct u_data {
	struct	p_tab *u_ptab;	/* Process table pointer. */
	char	u_insys;	/* True if in kernel. */
	char	u_callno;	/* Syscall being executed. */
	char	*u_retloc;	/* Return location from syscall. */
	int	u_retval;	/* Return value from syscall. */
	int	u_error;	/* Last error number. */
	char	*u_sp;		/* Used when a process is swapped. */
	char	*u_bc;		/* Place to save user's frame pointer. */
	int	u_argn;		/* Last arg. */
	int	u_argn1;	/* This way because args on stack backwards. */
	int	u_argn2;
	int	u_argn3;	/* Args n - 3, n - 2, n - 1, and n. */

	char	*u_base;	/* Source or destination for I/O. */
	unsigned u_count;	/* Amount for I/O. */
	off_t	u_offset;	/* Place in file for I/O. */
	struct	blkbuf *u_buf;

	int	u_gid;
	int	u_euid;
	int	u_egid;
	int	u_mask;		/* Umask: file creation mode mask. */
	time_t	u_time;		/* Start time. */
	/* Process file table: contains indexes into open file table. */
	char	u_files[UFTSIZE];
	inoptr	u_cwd;		/* Index into inode table of cwd. */
	char	*u_break;	/* Top of data space. */

	inoptr	u_ino;		/* Used during execve(). */
	char	*u_isp;		/* Value of initial sp (argv). */

	int	(*u_sigvec[NSIGS])(); /* Array of signal vectors. */
	int	u_cursig;	/* Signal currently being caught. */
	char	u_name[8];	/* Name invoked with. */
	time_t	u_utime;	/* Elapsed ticks in user mode. */
	time_t	u_stime;	/* Ticks in system mode. */
	time_t	u_cutime;	/* Total childrens ticks. */
	time_t	u_cstime;
} u_data;

/* Struct to temporarily hold arguments in execve. */
struct s_argblk {
	int	a_argc;
	int	a_arglen;
	int	a_envc;
	char	a_buf[512 - 3 * sizeof(int)];
};

/* Device driver switch table. */
typedef struct devsw {
	int	minor;		/* The minor device number (an arg to below). */
	int	(*dev_open)();	/* The routines for reading, etc. */
	int	(*dev_close)();	/* Format: op(minor,blkno,offset,count,buf); */
	int	(*dev_read)();	/* Offset would be ignored for block devices. */
	int	(*dev_write)();	/* blkno and offset ignored for tty, etc. */
	int	(*dev_ioctl)();	/* Count is rounded to 512 for block devices. */
} devsw;

/* open() parameters. */
#define O_RDONLY	0
#define O_WRONLY	1
#define O_RDWR		2

/* Error codes. */
#define EPERM		1               
#define ENOENT		2               
#define ESRCH		3               
#define EINTR		4               
#define EIO		5               
#define ENXIO		6               
#define E2BIG		7               
#define ENOEXEC		8               
#define EBADF		9               
#define ECHILD		10              
#define EAGAIN		11              
#define ENOMEM		12              
#define EACCES		13              
#define EFAULT		14              
#define ENOTBLK		15              
#define EBUSY		16              
#define EEXIST		17              
#define EXDEV		18              
#define ENODEV		19              
#define ENOTDIR		20              
#define EISDIR		21              
#define EINVAL		22              
#define ENFILE		23              
#define EMFILE		24              
#define ENOTTY		25              
#define ETXTBSY		26              
#define EFBIG		27              
#define ENOSPC		28              
#define ESPIPE		29              
#define EROFS		30              
#define EMLINK		31              
#define EPIPE		32              
#define ENAMETOOLONG	63              

#include "config.h"
