/**************************************************
UZI (Unix Z80 Implementation) Kernel:  filesys.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern void	bcopy(const void *, void *, int);
extern void	bzero(void *, int);

char *		bread();

inoptr		n_open(char *, inoptr *);
inoptr		srch_dir(inoptr, char *);
inoptr		srch_mt(inoptr);
inoptr		i_open(int, unsigned int);
int		ch_link(inoptr, char *, char *, inoptr);
char *		filename(char *);
int		namecomp(char *, char *);
inoptr		newfile(inoptr, char *);
fsptr		getdev(int);
int		baddev(fsptr);
unsigned int	i_alloc(int);
void		i_free(int, unsigned int);
blkno_t		blk_alloc(int);
void		blk_free(int, blkno_t);
int		oft_alloc(void);
void		oft_deref(int);
int		uf_alloc(void);
void		i_ref(inoptr);
void		i_deref(inoptr);
void		wr_inode(inoptr);
int		isdevice(inoptr);
int		devnum(inoptr);
void		f_trunc(inoptr);
void		freeblk(int, blkno_t, int);
blkno_t		bmap(inoptr, blkno_t, int);
void		validblk(int, blkno_t);
inoptr		getinode(int);
int		super(void);
int		getperm(inoptr);
void		setftime(inoptr, int);
int		getmode(inoptr);
int		fmount(int, inoptr);
void		magic(inoptr);

/*
 * n_open is given a string containing a path name,
 * and returns a inode table pointer.  If it returns NULL,
 * the file did not exist.  If the parent existed,
 * and parent is not null, parent will be filled in with
 * the parents inoptr. Otherwise, parent will be set to NULL.
 */
inoptr
n_open(char *name, inoptr *parent)
{
	register inoptr wd;	/* The directory we are currently searching. */
	register inoptr ninode;
	register inoptr temp;
	inoptr srch_dir();
	inoptr srch_mt();

	if (*name == '/')
		wd = root;
	else
		wd = udata.u_cwd;

	i_ref(ninode = wd);
	i_ref(ninode);

	for(;;) {
		if (ninode)
			magic(ninode);

		/* See if we are at a mount point. */
		if (ninode)
			ninode = srch_mt(ninode);

		while (*name == '/')	/* Skip (possibly repeated) slashes. */
			++name;
		ifnot (*name)		/* No more components of path? */
			break;
		ifnot (ninode) {
			udata.u_error = ENOENT;
			goto nodir;
		}
		i_deref(wd);
		wd = ninode;
		if (getmode(wd) != F_DIR) {
			udata.u_error = ENOTDIR;
			goto nodir;
		}
		ifnot (getperm(wd) & OTH_EX) {
			udata.u_error = EPERM;
			goto nodir;
		}

		/* See if we are going up through a mount point. */
		if ( wd->c_num == ROOTINODE && wd->c_dev != ROOTDEV && name[1] == '.') {
			temp = fs_tab[wd->c_dev].s_mntpt;
			++temp->c_refs;
			i_deref(wd);
			wd = temp;
		}

		ninode = srch_dir(wd, name);

		while (*name != '/' && *name )
			++name;
	}

	if (parent)
		*parent = wd;
	else
		i_deref(wd);
	ifnot (parent || ninode)
		udata.u_error = ENOENT;
	return (ninode);

nodir:
	if (parent)
		*parent = NULLINODE;
	i_deref(wd);
	return (NULLINODE);
}

/*
 * srch_dir is given a inode pointer of an open directory and a
 * string containing a filename, and searches the directory for
 * the file.  If it exists, it opens it and returns the inode
 * pointer, otherwise NULL.  This depends on the fact that ba_read
 * will return unallocated blocks as zero-filled, and a partially
 * allocated block will be padded with zeroes.
 */
inoptr
srch_dir(inoptr wd, char *compname)
{
	register int curentry;
	register blkno_t curblock;
	register struct direct *buf;
	register int nblocks;
	unsigned inum;
	inoptr i_open();
	blkno_t bmap();

	nblocks = wd->c_node.i_size.o_blkno;
	if (wd->c_node.i_size.o_offset)
		++nblocks;

	for (curblock = 0; curblock < nblocks; ++curblock) {
		buf = (struct direct *)bread( wd->c_dev,
		    bmap(wd, curblock, 1), 0);
		for (curentry = 0; curentry < 32; ++curentry) {
			if (namecomp(compname, buf[curentry].d_name)) {
				inum = buf[curentry&0x1f].d_ino;
				brelse(buf);
				return (i_open(wd->c_dev, inum));
			}
		}
		brelse(buf);
	}
	return (NULLINODE);
}

/*
 * srch_mt sees if the given inode is a mount point.
 * If so it dereferences it, and references and returns
 * a pointer to the root of the mounted filesystem.
 */
inoptr
srch_mt(inoptr ino)
{
	register int j;
	inoptr i_open();

	for (j = 0; j < NDEVS; ++j) {
		if (fs_tab[j].s_mounted == SMOUNTED &&
		    fs_tab[j].s_mntpt == ino) {
			i_deref(ino);
			return (i_open(j, ROOTINODE));
		}
	}
	return (ino);
}

/*
 * i_open is given an inode number and a device number,
 * and makes an entry in the inode table for them, or
 * increases its reference count if it is already there.
 * An inode # of zero means a newly allocated inode.
 */
inoptr
i_open(int dev, unsigned int ino)
{
	struct dinode *buf;
	register inoptr nindex;
	int i;
	register inoptr j;
	int new;
	static nexti;
	unsigned i_alloc();

	nexti = i_tab;

	if (dev < 0 || dev >= NDEVS)
		panic("i_open: Bad dev");

	new = 0;
	ifnot (ino) {	/* Want a new one */
		new = 1;
		ifnot (ino = i_alloc(dev)) {
			udata.u_error = ENOSPC;
			return (NULLINODE);
		}
	}

	if (ino < ROOTINODE || ino >= (fs_tab[dev].s_isize - 2) * 8) {
		warning("i_open: bad inode number");
		return (NULLINODE);
	}

	nindex = NULLINODE;
	j = nexti;
	for (i = 0; i < ITABSIZE; ++i) {
		nexti = j;
		if (++j >= i_tab + ITABSIZE)
			j = i_tab;

		ifnot (j->c_refs)
			nindex = j;

		if (j->c_dev == dev && j->c_num == ino) {
			nindex = j;
			goto found;
		}
	}

	/* Not already in table. */
	ifnot (nindex) {	/* No unrefed slots in inode table. */
		udata.u_error = ENFILE;
		return (NULLINODE);
	}

	buf = (struct dinode *)bread(dev, (ino>>3) + 2, 0);
	bcopy((char *)&(buf[ino & 0x07]), (char *)&(nindex->c_node), 64);
	brelse(buf);

	nindex->c_dev = dev;
	nindex->c_num = ino;
	nindex->c_magic = CMAGIC;
found:
	if (new) {
		if (nindex->c_node.i_nlink || nindex->c_node.i_mode & F_MASK)
			goto badino;
	} else {
		ifnot (nindex->c_node.i_nlink && nindex->c_node.i_mode & F_MASK)
			goto badino;
	}

	++nindex->c_refs;
	return (nindex);
badino:
	warning("i_open: bad disk inode");
	return (NULLINODE);
}

/*
 * ch_link modifies or makes a new entry in the directory for the name
 * and inode pointer given.  The directory is searched for oldname.
 * When found, it is changed to newname, and its inode # is that of
 * *nindex.  An oldname of "" matches an unused slot, and an nindex
 * of NULLINODE means an inode # of 0.  A return status of 0 means
 * there was no space left in the filesystem, or a non-empty oldname
 * was not found, or the user did not have write permission.
 */
int
ch_link(inoptr wd, char *oldname, char *newname, inoptr nindex)
{
	struct direct curentry;

	ifnot (getperm(wd) & OTH_WR) {
		udata.u_error = EPERM;
		return (0);
	}

	/* Search the directory for the desired slot. */
	udata.u_offset.o_blkno = 0;
	udata.u_offset.o_offset = 0;

	for (;;) {
		udata.u_count = 16;
		udata.u_base = (char *)&curentry;
		readi(wd);

		/* Read until EOF or name is found. */
		/* readi() advances udata.u_offset */
		if (udata.u_count == 0 || namecomp(oldname, curentry.d_name))
			break;
	}

	if (udata.u_count == 0 && *oldname)
		return (0);	/* Entry not found. */

	bcopy(newname, curentry.d_name, 14);
	if (nindex)
		curentry.d_ino = nindex->c_num;
	else
		curentry.d_ino = 0;

	/* If an existing slot is being used, we must back up the file offset. */
	if (udata.u_count) {
		ifnot (udata.u_offset.o_offset) {
			--udata.u_offset.o_blkno;
			udata.u_offset.o_offset = 512;
		}
		udata.u_offset.o_offset -= 16;
	}

	udata.u_count = 16;
	udata.u_base = (char *)&curentry;
	writei(wd);

	if (udata.u_error)
		return (0);

	setftime(wd, A_TIME|M_TIME|C_TIME);  /* Sets c_dirty. */

	/* Update file length to next block. */
	if (wd->c_node.i_size.o_offset) {
		wd->c_node.i_size.o_offset = 0;
		++wd->c_node.i_size.o_blkno;
	}

	return (1);
}

/*
 * filename is given a path name, and returns a pointer
 * to the final component of it.
 */
char *
filename(char *path)
{
	register char *ptr;

	ptr = path;
	while (*ptr)
		++ptr;
	while (*ptr != '/' && ptr-- > path)
		;
	return (ptr + 1);
}

/*
 * namecomp compares two strings to see if they are the same file name.
 * It stops at 14 chars or a null or a slash, and returns 0 for difference.
 */

int
namecomp(char *n1, char *n2)
{
	register int n;

	n = 14;
	while (*n1 && *n1 != '/') {
		if (*n1++ != *n2++)
			return (0);
		ifnot (--n)
			return (-1);
	}
	return (*n2 == '\0' || *n2 == '/');
}

/*
 * newfile is given a pointer to a directory and a name, and creates
 * an entry in the directory for the name, dereferences the parent,
 * and returns a pointer to the new inode.  It allocates an inode
 * number, and creates a new entry in the inode table for the new
 * file, and initializes the inode table entry for the new file.
 * The new file will have one reference, and 0 links to it.
 * Better make sure there isn't already an entry with the same name.
 */
inoptr
newfile(inoptr pino, char *name)
{
	register inoptr nindex;
	register int j;
	inoptr i_open();

	ifnot (nindex = i_open(pino->c_dev, 0))
		goto nogood;

	nindex->c_node.i_mode = F_REG;	/* For the time being. */
	nindex->c_node.i_nlink = 1;
	nindex->c_node.i_size.o_offset = 0;
	nindex->c_node.i_size.o_blkno = 0;
	for (j = 0; j < 20; j++)
		nindex->c_node.i_addr[j] = 0;
	wr_inode(nindex);

	ifnot (ch_link(pino,"",filename(name),nindex)) {
		i_deref(nindex);
		goto nogood;
	}

	i_deref(pino);
	return (nindex);
nogood:
	i_deref(pino);
	return (NULLINODE);
}

/*
 * getdev checks the given device number, and returns its address
 * in the mount table.  Also time-stamp the superblock of dev,
 * and mark it modified.
 * Used when freeing and allocating blocks and inodes.
 */
fsptr
getdev(int devno)
{
	register fsptr dev;

	dev = fs_tab + devno;
	if (devno < 0 || devno >= NDEVS || !dev->s_mounted)
		panic("getdev: bad dev");
	rdtime(&(dev->s_time));
	dev->s_fmod = 1;
	return (dev);
}

/*
 * baddev returns true if the superblock magic number is corrupt.
 */
int
baddev(fsptr dev)
{
	return (dev->s_mounted != SMOUNTED);
}

/*
 * i_alloc finds an unused inode number, and returns it,
 * or 0 if there are no more inodes available.
 */
unsigned int
i_alloc(int devno)
{
	fsptr dev;
	blkno_t blk;
	struct dinode *buf;
	register int j;
	register int k;
	unsigned int ino;

	if (baddev(dev = getdev(devno)))
		goto corrupt;
tryagain:
	if (dev->s_ninode) {
		ifnot (dev->s_tinode)
			goto corrupt;
		ino = dev->s_inode[--dev->s_ninode];
		if (ino < 2 || ino >= (dev->s_isize - 2) * 8)
			goto corrupt;
		--dev->s_tinode;
		return (ino);
	}

	/* We must scan the inodes, and fill up the table. */
	_sync();	/* Make on-disk inodes consistent. */
	k = 0;
	for (blk = 2; blk < dev->s_isize; blk++) {
		buf = (struct dinode *)bread(devno, blk, 0);
		for (j = 0; j < 8; j++) {
			ifnot (buf[j].i_mode || buf[j].i_nlink)
				dev->s_inode[k++] = 8 * (blk - 2) + j;
			if (k == 50) {
				brelse(buf);
				goto done;
			}
		}
		brelse(buf);
	}
done:
	ifnot (k) {
		if (dev->s_tinode)
			goto corrupt;
		udata.u_error = ENOSPC;
		return (0);
	}

	dev->s_ninode = k;
	goto tryagain;
corrupt:
	warning("i_alloc: corrupt superblock");
	dev->s_mounted = 1;
	udata.u_error = ENOSPC;
	return (0);
}

/*
 * i_free is given a device and inode number, and frees the inode.
 * It is assumed that there are no references to the inode in the
 * inode table or in the filesystem.
 */
void
i_free(int devno, unsigned int ino)
{
	register fsptr dev;

	if (baddev(dev = getdev(devno)))
		return;

	if (ino < 2 || ino >= (dev->s_isize - 2) * 8)
		panic("i_free: bad ino");

	++dev->s_tinode;
	if (dev->s_ninode < 50)
		dev->s_inode[dev->s_ninode++] = ino;
}

/*
 * blk_alloc is given a device number, and allocates an unused block
 * from it.  A returned block number of zero means no more blocks.
 */
blkno_t
blk_alloc(int devno)
{
	register fsptr dev;
	register blkno_t newno;
	blkno_t *buf;
	register int j;

	if (baddev(dev = getdev(devno)))
		goto corrupt2;

	if (dev->s_nfree <= 0 || dev->s_nfree > 50)
		goto corrupt;

	newno = dev->s_free[--dev->s_nfree];
	ifnot (newno) {
		if (dev->s_tfree != 0)
			goto corrupt;
		udata.u_error = ENOSPC;
		++dev->s_nfree;
		return (0);
	}

	/* See if we must refill the s_free array. */
	ifnot (dev->s_nfree) {
		buf = (blkno_t *)bread(devno, newno, 0);
		dev->s_nfree = buf[0];
		for (j = 0; j < 50; j++)
			dev->s_free[j] = buf[j + 1];
		brelse((char *)buf);
	}

	validblk(devno, newno);

	ifnot (dev->s_tfree)
		goto corrupt;
	--dev->s_tfree;

	/* Zero out the new block. */
	buf = (blkno_t *)bread(devno, newno, 2);
	bzero(buf, 512);
	bawrite(buf);
	return (newno);
corrupt:
	warning("blk_alloc: corrupt");
	dev->s_mounted = 1;
corrupt2:
	udata.u_error = ENOSPC;
	return (0);
}

/*
 * blk_free is given a device number and a block number,
 * and frees the block.
 */
void
blk_free(int devno, blkno_t blk)
{
	register fsptr dev;
	register char *buf;

	ifnot (blk)
		return;

	if (baddev(dev = getdev(devno)))
		return;

	validblk(devno, blk);

	if (dev->s_nfree == 50) {
		buf = bread(devno, blk, 1);
		bcopy((char *)&(dev->s_nfree), buf, 512);
		bawrite(buf);
		dev->s_nfree = 0;
	}

	++dev->s_tfree;
	dev->s_free[(dev->s_nfree)++] = blk;
}

/*
 * oft_alloc allocates, and possibly frees, entries
 * in the open file table.
 */
int
oft_alloc(void)
{
	register int j;

	for (j = 0; j < OFTSIZE; ++j) {
		ifnot (of_tab[j].o_refs) {
			of_tab[j].o_refs = 1;
			of_tab[j].o_inode = NULLINODE;
			return (j);
		}
	}
	udata.u_error = ENFILE;
	return (-1);
}

/*
 * oft_deref dereferences, and possibly frees, entries
 * in the open file table.
 */
void
oft_deref(int of)
{
	register struct oft *ofptr;

	ofptr = of_tab + of;

	if (!(--ofptr->o_refs) && ofptr->o_inode) {
		i_deref(ofptr->o_inode);
		ofptr->o_inode = NULLINODE;
	}
}

/*
 * uf_alloc finds an unused slot in the user file table.
 */
int
uf_alloc(void)
{
	register int j;

	for (j = 0; j < UFTSIZE; ++j)
		/* Portable, unlike  == -1. */
		if (udata.u_files[j] & 0x80)
			return (j);
	udata.u_error = ENFILE;
	return (-1);
}

/*
 * i_ref increases the reference count of the given inode table entry.
 */
void
i_ref(inoptr ino)
{
	if (++(ino->c_refs) == 2 * ITABSIZE)	/* Arbitrary limit. */
		panic("too many i-refs");
}

/*
 * i_deref decreases the reference count of an inode, and frees it from
 * the table if there are no more references to it.  If it also has no
 * links, the inode itself and its blocks (if not a device) is freed.
 */
void
i_deref(inoptr ino)
{
	magic(ino);

	ifnot (ino->c_refs)
		panic("inode freed");

	if ((ino->c_node.i_mode & F_MASK) == F_PIPE)
		wakeup((char *)ino);

	/*
	 * If the inode has no links and no refs, it must have
	 * its blocks freed.
	 */
	ifnot (--ino->c_refs || ino->c_node.i_nlink)
		f_trunc(ino);

	/* If the inode was modified, we must write it to disk. */
	if (!(ino->c_refs) && ino->c_dirty) {
		ifnot (ino->c_node.i_nlink) {
			ino->c_node.i_mode = 0;
			i_free(ino->c_dev, ino->c_num);
		}
		wr_inode(ino);
	}
}

/*
 * wr_inode writes out the given inode in the inode table out to disk,
 * and resets its dirty bit.
 */
void
wr_inode(inoptr ino)
{
	struct dinode *buf;
	register blkno_t blkno;

	magic(ino);

	blkno = (ino->c_num >> 3) + 2;
	buf = (struct dinode *)bread(ino->c_dev, blkno, 0);
	bcopy((char *)(&ino->c_node),
	    (char *)((char **)&buf[ino->c_num & 0x07]), 64);
	bfree(buf, 2);
	ino->c_dirty = 0;
}

/*
 * isdevice returns true if it points to a device.
 */
int
isdevice(inoptr ino)
{
	return (ino->c_node.i_mode & 020000);
}

/*
 * devnum returns the device number of an inode representing a device.
 */
int
devnum(inoptr ino)
{
	return (*(ino->c_node.i_addr));
}

/*
 * f_trunc frees all the blocks associated with the file,
 * if it is a disk file.
 */
void
f_trunc(inoptr ino)
{
	int dev;
	int j;

	dev = ino->c_dev;

	/* First deallocate the double indirect blocks. */
	freeblk(dev, ino->c_node.i_addr[19], 2);

	/* Also deallocate the indirect blocks. */
	freeblk(dev, ino->c_node.i_addr[18], 1);

	/* Finally, free the direct blocks. */
	for (j = 17; j >= 0; --j)
		freeblk(dev, ino->c_node.i_addr[j], 0);

	bzero((char *)ino->c_node.i_addr, sizeof(ino->c_node.i_addr));

	ino->c_dirty = 1;
	ino->c_node.i_size.o_blkno = 0;
	ino->c_node.i_size.o_offset = 0;
}

/*
 * Companion function to f_trunc().
 */
void
freeblk(int dev, blkno_t blk, int level)
{
	blkno_t *buf;
	int j;

	ifnot (blk)
		return;

	if (level) {
		buf = (blkno_t *)bread(dev, blk, 0);
		for (j = 255; j >= 0; --j)
			freeblk(dev, buf[j], level - 1);
		brelse((char *)buf);
	}

	blk_free(dev, blk);
}

/* XXX - Changes: blk_alloc zeroes block it allocates */

/*
 * bmap defines the structure of file system storage by
 * returning the physical block number on a device given
 * the inode and the logical block number in a file.
 * The block is zeroed if created.
 */
blkno_t
bmap(inoptr ip, blkno_t bn, int rwflg)
{
	register int i;
	register bufptr bp;
	register int j;
	register blkno_t nb;
	int sh;
	int dev;

	blkno_t blk_alloc();

	if (getmode(ip) == F_BDEV)
		return (bn);

	dev = ip->c_dev;

	/* Blocks 0..17 are direct blocks. */
	if (bn < 18) {
		nb = ip->c_node.i_addr[bn];
		if (nb == 0) {
			if (rwflg || (nb = blk_alloc(dev)) == 0)
				return (NULLBLK);
			ip->c_node.i_addr[bn] = nb;
			ip->c_dirty = 1;
		}
		return (nb);
	}

	/*
	 * Addresses 18 and 19 have single and double indirect blocks.
	 * The first step is to determine how many levels of indirection.
	 */
	bn -= 18;
	sh = 0;
	j = 2;
	/* bn > 255  so double indirect. */
	if (bn & 0xff00) {
		sh = 8;
		bn -= 256;
		j = 1;
	}

	/*
	 * Fetch the address from the inode.
	 * Create the first indirect block if needed.
	 */
	ifnot (nb = ip->c_node.i_addr[20 - j]) {
		if(rwflg || !(nb = blk_alloc(dev)))
			return (NULLBLK);
		ip->c_node.i_addr[20 - j] = nb;
		ip->c_dirty = 1;
	}

	/*
	 * Fetch through the indirect blocks.
	 */
	for (; j <= 2; j++) {
		bp = (bufptr)bread(dev, nb, 0);
		/****** XXX - Why is this commented out
		if(bp->bf_error) {
			brelse(bp);
			return ((blkno_t)0);
		}
	        ******/
		i = (bn >> sh) & 0xff;
		if (nb = ((blkno_t *)bp)[i])
			brelse(bp);
		else {
			if(rwflg || !(nb = blk_alloc(dev))) {
				brelse(bp);
				return (NULLBLK);
			}
			((blkno_t *)bp)[i] = nb;
			bawrite(bp);
		}
		sh -= 8;
	}
	return (nb);
}

/*
 * validblk panics if the given block number is not a valid
 * data block for the given device.
 */
void
validblk(int dev, blkno_t num)
{
	register fsptr devptr;

	devptr = fs_tab + dev;

	if (devptr->s_mounted == 0)
		panic("validblk: not mounted");

	if ((num < devptr->s_isize) || (num >= devptr->s_fsize))
		panic("validblk: invalid block");
}

/*
 * getinode returns the inode pointer associated with a user's
 * file descriptor, checking for valid data structures.
 */
inoptr
getinode(int uindex)
{
	register int oftindex;
	register inoptr inoindex;

	if ((uindex < 0) || (uindex >= UFTSIZE) ||
	    (udata.u_files[uindex] & 0x80)) {
		udata.u_error = EBADF;
		return (NULLINODE);
	}

	if (((oftindex = udata.u_files[uindex]) < 0) || (oftindex >= OFTSIZE))
		panic("getinode: bad descriptor table");

	if (((inoindex = of_tab[oftindex].o_inode) < i_tab) ||
	    (inoindex >= i_tab + ITABSIZE))
		panic("getinode: bad OFT");

	magic(inoindex);

	return (inoindex);
}

/*
 * super returns true if we are the superuser.
 */
int
super(void)
{
	return (udata.u_euid == 0);
}

/*
 * getperm looks at the given inode and the effective user/group ids,
 * and returns the effective permissions in the low-order 3 bits.
 */
int
getperm(inoptr ino)
{
	int mode;

	if (super())
		return (07);

	mode = ino->c_node.i_mode;
	if (ino->c_node.i_uid == udata.u_euid)
		mode >>= 6;
	else if (ino->c_node.i_gid == udata.u_egid)
		mode >>= 3;

	return (mode & 07);
}

/*
 * setftime sets the times of the given inode, according to the flags.
 */
void
setftime(inoptr ino, int flag)
{
	ino->c_dirty = 1;

	if (flag & A_TIME)
		rdtime(&(ino->c_node.i_atime));
	if (flag & C_TIME)
		rdtime(&(ino->c_node.i_ctime));
	if (flag & M_TIME)
		rdtime(&(ino->c_node.i_mtime));
}

/*
 * getmode returns the given inode's mode.
 */
int
getmode(inoptr ino)
{
	return (ino->c_node.i_mode & F_MASK);
}

/*
 * fmount places the given device in the mount table with
 * mount point ino.
 */
int
fmount(int dev, inoptr ino)
{
	char *buf;
	register struct filesys *fp;

	if (d_open(dev) != 0)
		panic("fmount: Cant open filesystem");
	/* Dev 0 blk 1 */
	fp = fs_tab + dev;
	buf = bread(dev, 1, 0);
	bcopy(buf, (char *)fp, sizeof(struct filesys));
	brelse(buf);

	/* See if there really is a filesystem on the device. */
	if (fp->s_mounted != SMOUNTED || fp->s_isize >= fp->s_fsize)
		return (-1);

	fp->s_mntpt = ino;
	if (ino)
		++ino->c_refs;
	return (0);
}

/*
 * magic checks if the given inode is corrupt.
 */
void
magic(inoptr ino)
{
	if (ino->c_magic != CMAGIC)
		panic("magic: Corrupt inode");
}
