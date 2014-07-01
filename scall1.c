/**************************************************
UZI (Unix Z80 Implementation) Kernel:  scall1.c
***************************************************/

#include "unix.h"
#include "extern.h"

extern void	bcopy(const void *, void *, int);

/*******************************************
open(char *name, int16 flag)
*********************************************/

#define name (char *)udata.u_argn1
#define flag (int16)udata.u_argn

_open()
{
	int16 uindex;
	int16 oftindex;
	inoptr ino;
	int16 perm;
	inoptr n_open();

	if (flag < 0 || flag > 2) {
		udata.u_error = EINVAL;
		return (-1);
	}
	if ((uindex = uf_alloc()) == -1)
		return (-1);

	if ((oftindex = oft_alloc()) == -1)
		goto nooft;

	ifnot (ino = n_open(name, NULLINOPTR))
		goto cantopen;

	of_tab[oftindex].o_inode = ino;

	perm = getperm(ino);
	if (((flag == O_RDONLY || flag == O_RDWR) && !(perm & OTH_RD)) ||
	    ((flag == O_WRONLY || flag == O_RDWR) && !(perm & OTH_WR))) {
		udata.u_error = EPERM;
		goto cantopen;
	}

	if (getmode(ino) == F_DIR &&
	    (flag == O_WRONLY || flag == O_RDWR)) {
		udata.u_error = EISDIR;
		goto cantopen;
	}

	if (isdevice(ino) && d_open((int)ino->c_node.i_addr[0]) != 0) {
		udata.u_error = ENXIO;
		goto cantopen;
	}

	udata.u_files[uindex] = oftindex;

	of_tab[oftindex].o_ptr.o_offset = 0;
	of_tab[oftindex].o_ptr.o_blkno = 0;
	of_tab[oftindex].o_access = flag;

	return (uindex);

cantopen:
	oft_deref(oftindex);	/* This will call i_deref(). */
nooft:
	udata.u_files[uindex] = -1;
	return (-1);
}

#undef name
#undef flag

/*********************************************
close(int16 uindex)
**********************************************/

#define uindex (int16)udata.u_argn

_close()
{
	return (doclose(uindex));
}

#undef uindex

doclose(int16 uindex)
{
	int16 oftindex;
	inoptr ino;
	inoptr getinode();

	ifnot (ino = getinode(uindex))
		return (-1);
	oftindex = udata.u_files[uindex];

	if (isdevice(ino)
	    /* && ino->c_refs == 1 && of_tab[oftindex].o_refs == 1 */)
		d_close((int)(ino->c_node.i_addr[0]));

	udata.u_files[uindex] = -1;
	oft_deref(oftindex);

	return (0);
}

/****************************************
creat(char *name, int16 mode)
*****************************************/

#define name (char *)udata.u_argn1
#define mode (int16)udata.u_argn

_creat()
{
	inoptr ino;
	int16 uindex;
	int16 oftindex;
	inoptr parent;
	int16 j;
	inoptr n_open();
	inoptr newfile();

	parent = NULLINODE;

	if ((uindex = uf_alloc()) == -1)
		return (-1);
	if ((oftindex = oft_alloc()) == -1)
		return (-1);

	/* The file exists. */
	if (ino = n_open(name, &parent)) {
		i_deref(parent);
		if (getmode(ino) == F_DIR) {
			i_deref(ino);
			udata.u_error = EISDIR;
			goto nogood;
		}
		ifnot (getperm(ino) & OTH_WR) {
			i_deref(ino);
			udata.u_error = EACCES;
			goto nogood;
		}
		if (getmode(ino) == F_REG) {
			/* Truncate the file to zero length. */
			f_trunc(ino);
			/* Reset any oft pointers. */
			for (j = 0; j < OFTSIZE; ++j)
				if (of_tab[j].o_inode == ino)
					of_tab[j].o_ptr.o_blkno =
					    of_tab[j].o_ptr.o_offset = 0;
		}
	} else {
		if (parent && (ino = newfile(parent, name))) {
			/* Parent was dereferenced in newfile. */
			ino->c_node.i_mode =
			    (F_REG | (mode & MODE_MASK & ~udata.u_mask));
			setftime(ino, A_TIME|M_TIME|C_TIME);
			/* The rest of the inode is initialized in newfile. */
			wr_inode(ino);
		} else {
			/* Doesn't exist and can't make it. */
			if (parent)
				i_deref(parent);
			goto nogood;
		}
	}

	udata.u_files[uindex] = oftindex;

	of_tab[oftindex].o_ptr.o_offset = 0;
	of_tab[oftindex].o_ptr.o_blkno = 0;
	of_tab[oftindex].o_inode = ino;
	of_tab[oftindex].o_access = O_WRONLY;

	return (uindex);
nogood:
	oft_deref(oftindex);
	return (-1);
}

#undef name
#undef mode

/********************************************
pipe(int fildes[])
*******************************************/

#define fildes (int *)udata.u_argn

_pipe()
{
	int16 u1, u2, oft1, oft2;
	inoptr ino;
	inoptr i_open();

	if ((u1 = uf_alloc()) == -1)
		goto nogood2;
	if ((oft1 = oft_alloc()) == -1)
		goto nogood2;
	udata.u_files[u1] = oft1;

	if ((u2 = uf_alloc()) == -1)
		goto nogood;
	if ((oft2 = oft_alloc()) == -1) {
		oft_deref(oft1);
		goto nogood;
	}

	ifnot (ino = i_open(ROOTDEV, 0)) {
		oft_deref(oft1);
		oft_deref(oft2);
		goto nogood;
	}

	udata.u_files[u2] = oft2;

	of_tab[oft1].o_ptr.o_offset = 0;
	of_tab[oft1].o_ptr.o_blkno = 0;
	of_tab[oft1].o_inode = ino;
	of_tab[oft1].o_access = O_RDONLY;

	of_tab[oft2].o_ptr.o_offset = 0;
	of_tab[oft2].o_ptr.o_blkno = 0;
	of_tab[oft2].o_inode = ino;
	of_tab[oft2].o_access = O_WRONLY;

	++ino->c_refs;
	/* No permissions necessary on pipes. */
	ino->c_node.i_mode = F_PIPE | 0777;
	/* A pipe is not in any directory. */
	ino->c_node.i_nlink = 0;

	*fildes = u1;
	*(fildes + 1) = u2;
	return (0);
nogood:
	udata.u_files[u1] = -1;
nogood2:
	return (-1);
}

#undef fildes

/********************************************
link(char *name1, char *name2)
*********************************************/

#define name1 (char *)udata.u_argn1
#define name2 (char *)udata.u_argn

_link()
{
	inoptr ino;
	inoptr ino2;
	inoptr parent2;
	char *filename();
	inoptr n_open();

	ifnot (ino = n_open(name1, NULLINOPTR))
		return (-1);

	if (getmode(ino) == F_DIR && !super()) {
		udata.u_error = EPERM;
		goto nogood;
	}

	/* Make sure file2 doesn't exist, and get its parent. */
	if (ino2 = n_open(name2, &parent2)) {
		i_deref(ino2);
		i_deref(parent2);
		udata.u_error = EEXIST;
		goto nogood;
	}

	ifnot (parent2)
		goto nogood;

	if (ino->c_dev != parent2->c_dev) {
		i_deref(parent2);
		udata.u_error = EXDEV;
		goto nogood;
	}

	if (ch_link(parent2, "", filename(name2), ino) == 0)
		goto nogood;

	/* Update the link count. */
	++ino->c_node.i_nlink;
	wr_inode(ino);
	setftime(ino, C_TIME);

	i_deref(parent2);
	i_deref(ino);
	return (0);
nogood:
	i_deref(ino);
	return (-1);
}

#undef name1
#undef name2

/**********************************************************
unlink(char *path)
**************************************************/

#define path (char *)udata.u_argn

_unlink()
{
	inoptr ino;
	inoptr pino;
	char *filename();
	inoptr i_open();
	inoptr n_open();

	ino = n_open(path, &pino);

	ifnot (pino && ino) {
		udata.u_error = ENOENT;
		return (-1);
	}

	if (getmode(ino) == F_DIR && !super()) {
		udata.u_error = EPERM;
		goto nogood;
	}

	/* Remove the directory entry. */
	if (ch_link(pino, filename(path), "", NULLINODE) == 0)
		goto nogood;

	/* Decrease the link count of the inode. */
	ifnot (ino->c_node.i_nlink--) {
		ino->c_node.i_nlink += 2;
		warning("_unlink: bad nlink");
	}
	setftime(ino, C_TIME);
	i_deref(pino);
	i_deref(ino);
	return (0);
nogood:
	i_deref(pino);
	i_deref(ino);
	return (-1);
}

#undef path

/*****************************************************
read(int16 d, char *buf, uint16 nbytes)
**********************************************/

#define d (int16)udata.u_argn2
#define buf (char *)udata.u_argn1
#define nbytes (uint16)udata.u_argn

_read()
{
	inoptr ino;
	inoptr rwsetup();

	/* Set up u_base, u_offset, ino; check permissions, file num. */
	if ((ino = rwsetup(1)) == NULLINODE)
		return (-1);	/* Bomb out if error. */

	readi(ino);
	updoff();
	return (udata.u_count);
}

#undef d
#undef buf
#undef nbytes

/***********************************
write(int16 d, char *buf, uint16 nbytes)
***********************************/

#define d (int16)udata.u_argn2
#define buf (char *)udata.u_argn1
#define nbytes (uint16)udata.u_argn

_write()
{
	inoptr ino;
	off_t *offp;
	inoptr rwsetup();

	/* Set up u_base, u_offset, ino; check permissions, file num. */
	if ((ino = rwsetup(0)) == NULLINODE)
		return (-1);	/* Bomb out if error. */

	writei(ino);
	updoff();
	return (udata.u_count);
}

#undef d
#undef buf
#undef nbytes

inoptr
rwsetup(int rwflag)
{
	inoptr ino;
	struct oft *oftp;
	inoptr getinode();

	udata.u_base = (char *)udata.u_argn1;	/* buf */
	udata.u_count = (uint16)udata.u_argn;	/* nbytes */

	if ((ino = getinode(udata.u_argn2)) == NULLINODE)
		return (NULLINODE);

	oftp = of_tab + udata.u_files[udata.u_argn2];
	if (oftp->o_access == (rwflag ? O_WRONLY : O_RDONLY)) {
		udata.u_error = EBADF;
		return (NULLINODE);
	}

	setftime(ino, rwflag ? A_TIME : (A_TIME | M_TIME | C_TIME));

	/* Initialize u_offset from file pointer. */
	udata.u_offset.o_blkno = oftp->o_ptr.o_blkno;
	udata.u_offset.o_offset = oftp->o_ptr.o_offset;
	return (ino);
}

/* XXX - needs more i/o error handling. */
readi(inoptr ino)
{
	uint16 amount;
	uint16 toread;
	blkno_t pblk;
	char *bp;
	int dev;
	int ispipe;
	char *bread();
	char *zerobuf();
	blkno_t bmap();

	dev = ino->c_dev;
	ispipe = 0;
	switch (getmode(ino)) {
	case F_DIR:
	case F_REG:
		/* See if end of file will limit read. */
		toread = udata.u_count =
		    ino->c_node.i_size.o_blkno - udata.u_offset.o_blkno >= 64 ?
		    udata.u_count : min(udata.u_count,
		    512 * (ino->c_node.i_size.o_blkno - udata.u_offset.o_blkno) +
		    (ino->c_node.i_size.o_offset - udata.u_offset.o_offset));
		goto loop;
	case F_PIPE:
		ispipe = 1;
		while (psize(ino) == 0) {
			if (ino->c_refs == 1)	/* No writers. */
				break;
			/* Sleep if empty pipe. */
			psleep(ino);
		}
		toread = udata.u_count = min(udata.u_count, psize(ino));
		goto loop;
	case F_BDEV:
		toread = udata.u_count;
		dev = *(ino->c_node.i_addr);
loop:
		while (toread) {
			if ((pblk = bmap(ino, udata.u_offset.o_blkno, 1)) != NULLBLK)
				bp = bread(dev, pblk, 0);
			else
				bp = zerobuf();

			bcopy(bp + udata.u_offset.o_offset, udata.u_base,
			    (amount = min(toread, 512 - udata.u_offset.o_offset)));
			brelse(bp);

			udata.u_base += amount;
			addoff(&udata.u_offset, amount);
			if (ispipe && udata.u_offset.o_blkno >= 18)
				udata.u_offset.o_blkno = 0;
			toread -= amount;
			if (ispipe) {
				addoff(&(ino->c_node.i_size), -amount);
				wakeup(ino);
			}
		}
		break;
	case F_CDEV:
		udata.u_count = cdread(ino->c_node.i_addr[0]);
		if (udata.u_count != -1)
			addoff(&udata.u_offset, udata.u_count);
		break;
	default:
		udata.u_error = ENODEV;
	}
}

/* XXX - needs more i/o error handling. */
writei(inoptr ino)
{
	uint16 amount;
	uint16 towrite;
	char *bp;
	int ispipe;
	blkno_t pblk;
	int created;	/* Set by bmap if newly allocated block used. */
	int dev;
	char *zerobuf();
	char *bread();
	blkno_t bmap();

	dev = ino->c_dev;

	switch (getmode(ino)) {
	case F_BDEV:
		dev = *(ino->c_node.i_addr);
	case F_DIR:
	case F_REG:
		ispipe = 0;
		towrite = udata.u_count;
		goto loop;
	case F_PIPE:
		ispipe = 1;
		while ((towrite = udata.u_count) > (16 * 512) - psize(ino)) {
			if (ino->c_refs == 1) {	/* No readers. */
				udata.u_count = -1;
				udata.u_error = EPIPE;
				ssig(udata.u_ptab, SIGPIPE);
				return;
			}
			/* Sleep if empty pipe. */
			psleep(ino);
		}
		/* Sleep if empty pipe. */
		goto loop;
loop:
		while (towrite) {
			amount = min(towrite, 512 - udata.u_offset.o_offset);

			if ((pblk = bmap(ino, udata.u_offset.o_blkno, 0)) == NULLBLK)
				break;	/* No space to make more blocks. */
			/*
			 * If we are writing an entire block,
			 * we don't care about its previous contents.
			 */
			bp = bread(dev, pblk, (amount == 512));

			bcopy(udata.u_base, bp + udata.u_offset.o_offset, amount);
			bawrite(bp);

			udata.u_base += amount;
			addoff(&udata.u_offset, amount);
			if (ispipe) {
				if (udata.u_offset.o_blkno >= 18)
					udata.u_offset.o_blkno = 0;
				addoff(&(ino->c_node.i_size), amount);
				/* Wake up any readers. */
				wakeup(ino);
			}
			towrite -= amount;
		}

		/* Update size if file grew. */
		ifnot (ispipe) {
			if (udata.u_offset.o_blkno > ino->c_node.i_size.o_blkno ||
			    (udata.u_offset.o_blkno == ino->c_node.i_size.o_blkno &&
			    udata.u_offset.o_offset > ino->c_node.i_size.o_offset)) {
				ino->c_node.i_size.o_blkno = udata.u_offset.o_blkno;
				ino->c_node.i_size.o_offset = udata.u_offset.o_offset;
				ino->c_dirty = 1;
			}
		}
		break;
	case F_CDEV:
		udata.u_count = cdwrite(ino->c_node.i_addr[0]);
		if (udata.u_count != -1)
			addoff(&udata.u_offset, udata.u_count);
		break;
	default:
		udata.u_error = ENODEV;
	}
}

min(int a, int b)
{
	return (a < b ? a : b);
}

psize(inoptr ino)
{
	return (512 * ino->c_node.i_size.o_blkno +
	    ino->c_node.i_size.o_offset);
}

addoff(off_t *ofptr, int amount)
{
	if (amount >= 0) {
		ofptr->o_offset += amount % 512;
		if (ofptr->o_offset >= 512) {
			ofptr->o_offset -= 512;
			++ofptr->o_blkno;
		}
		ofptr->o_blkno += amount / 512;
	} else {
		ofptr->o_offset -= (-amount) % 512;
		if (ofptr->o_offset < 0) {
			ofptr->o_offset += 512;
			--ofptr->o_blkno;
		}
		ofptr->o_blkno -= (-amount) / 512;
	}
}

updoff()
{
	off_t *offp;

	/* Update current file pointer. */
	offp = &of_tab[udata.u_files[udata.u_argn2]].o_ptr;
	offp->o_blkno = udata.u_offset.o_blkno;
	offp->o_offset = udata.u_offset.o_offset;
}

/****************************************
seek(int16 file, uint16 offset, int16 flag)
*****************************************/

#define file (int16)udata.u_argn2
#define offset (uint16)udata.u_argn1
#define flag (int16)udata.u_argn

_seek()
{
	inoptr ino;
	int16 oftno;
	uint16 retval;
	inoptr getinode();

	if ((ino = getinode(file)) == NULLINODE)
	return (-1);

	if (getmode(ino) == F_PIPE) {
		udata.u_error = ESPIPE;
		return (-1);
	}

	oftno = udata.u_files[file];


	if (flag <= 2)
		retval = of_tab[oftno].o_ptr.o_offset;
	else
		retval = of_tab[oftno].o_ptr.o_blkno;

	switch (flag) {
	case 0:
		of_tab[oftno].o_ptr.o_blkno = 0;
		of_tab[oftno].o_ptr.o_offset = offset;
		break;
	case 1:
		of_tab[oftno].o_ptr.o_offset += offset;
		break;
	case 2:
		of_tab[oftno].o_ptr.o_blkno = ino->c_node.i_size.o_blkno;
		of_tab[oftno].o_ptr.o_offset = ino->c_node.i_size.o_offset + offset;
		break;
	case 3:
		of_tab[oftno].o_ptr.o_blkno = offset;
		break;
	case 4:
		of_tab[oftno].o_ptr.o_blkno += offset;
		break;
	case 5:
		of_tab[oftno].o_ptr.o_blkno = ino->c_node.i_size.o_blkno + offset;
		break;
	default:
		udata.u_error = EINVAL;
		return (-1);
	}

	while ((unsigned)of_tab[oftno].o_ptr.o_offset >= 512) {
		of_tab[oftno].o_ptr.o_offset -= 512;
		++of_tab[oftno].o_ptr.o_blkno;
	}

	return ((int16)retval);
}

#undef file
#undef offset
#undef flag

/************************************
chdir(char *dir)
************************************/

#define dir (char *)udata.u_argn

_chdir()
{
	inoptr newcwd;
	inoptr n_open();

	ifnot (newcwd = n_open(dir, NULLINOPTR))
		return (-1);

	if (getmode(newcwd) != F_DIR) {
		udata.u_error = ENOTDIR;
		i_deref(newcwd);
		return (-1);
	}
	i_deref(udata.u_cwd);
	udata.u_cwd = newcwd;
	return (0);
}

#undef dir

/*************************************
mknod(char *name, int16 mode, int16 dev)
***************************************/

#define name (char *)udata.u_argn2
#define mode (int16)udata.u_argn1
#define dev (int16)udata.u_argn

_mknod()
{
	inoptr ino;
	inoptr parent;
	inoptr n_open();
	inoptr newfile();

	udata.u_error = 0;
	ifnot (super()) {
		udata.u_error = EPERM;
		return (-1);
	}

	if (ino = n_open(name, &parent)) {
		udata.u_error = EEXIST;
		goto nogood;
	}

	ifnot (parent) {
		udata.u_error = ENOENT;
		goto nogood3;
	}

	ifnot (ino = newfile(parent, name))
		goto nogood2;

	/* Initialize mode and dev. */
	ino->c_node.i_mode = mode & ~udata.u_mask;
	ino->c_node.i_addr[0] = isdevice(ino) ? dev : 0;
	setftime(ino, A_TIME|M_TIME|C_TIME);
	wr_inode(ino);

	i_deref(ino);
	return (0);
nogood:
	i_deref(ino);
nogood2:
	i_deref(parent);
nogood3:
	return (-1);
}

#undef name
#undef mode
#undef dev

/****************************************
sync()
***************************************/

_sync()
{
	int j;
	inoptr ino;
	char *buf;
	char *bread();

	/* Write out modified inodes. */
	for (ino = i_tab; ino < i_tab + ITABSIZE; ++ino) {
		if ((ino->c_refs) > 0 && ino->c_dirty != 0) {
			wr_inode(ino);
			ino->c_dirty = 0;
		}
	}

	/* Write out modified super blocks. */
	/* This fills the rest of the super block with garbage. */
	for (j = 0; j < NDEVS; ++j) {
		if (fs_tab[j].s_mounted == SMOUNTED && fs_tab[j].s_fmod) {
			fs_tab[j].s_fmod = 0;
			buf = bread(j, 1, 1);
			bcopy((char *)&fs_tab[j], buf, 512);
			bfree(buf, 2);
		}
	}

	bufsync();	/* Clear buffer pool. */
}

/****************************************
access(char *path, int16 mode)
****************************************/

#define path (char *)udata.u_argn1
#define mode (int16)udata.u_argn

_access()
{
	inoptr ino;
	int16 euid;
	int16 egid;
	int16 retval;
	inoptr n_open();

	if ((mode & 07) && !*(path)) {
		udata.u_error = ENOENT;
		return (-1);
	}

	/* Temporarily make eff. Id real id. */
	euid = udata.u_euid;
	egid = udata.u_egid;
	udata.u_euid = udata.u_ptab->p_uid;
	udata.u_egid = udata.u_gid;

	ifnot (ino = n_open(path, NULLINOPTR)) {
		retval = -1;
		goto nogood;
	}

	retval = 0;
	if (~getperm(ino) & (mode & 07)) {
		udata.u_error = EPERM;
		retval = -1;
	}

	i_deref(ino);
nogood:
	udata.u_euid = euid;
	udata.u_egid = egid;

	return (retval);
}

#undef path
#undef mode

/*******************************************
chmod(char *path, int16 mode)
*******************************************/

#define path (char *)udata.u_argn1
#define mode (int16)udata.u_argn

_chmod()
{
	inoptr ino;
	inoptr n_open();

	ifnot (ino = n_open(path, NULLINOPTR))
		return (-1);

	if (ino->c_node.i_uid != udata.u_euid && !super()) {
		i_deref(ino);
		udata.u_error = EPERM;
		return (-1);
	}

	ino->c_node.i_mode = (mode & MODE_MASK) | (ino->c_node.i_mode & F_MASK);
	setftime(ino, C_TIME);
	i_deref(ino);
	return (0);
}

#undef path
#undef mode

/***********************************************
chown(char *path, int owner, int group)
**********************************************/

#define path (char *)udata.u_argn2
#define owner (int16)udata.u_argn1
#define group (int16)udata.u_argn

_chown()
{
	inoptr ino;
	inoptr n_open();

	ifnot (ino = n_open(path, NULLINOPTR))
	return (-1);

	if (ino->c_node.i_uid != udata.u_euid && !super()) {
		i_deref(ino);
		udata.u_error = EPERM;
		return (-1);
	}

	ino->c_node.i_uid = owner;
	ino->c_node.i_gid = group;
	setftime(ino, C_TIME);
	i_deref(ino);
	return (0);
}

#undef path
#undef owner
#undef group

/**************************************
stat(char *path, char *buf)
****************************************/

#define path (char *)udata.u_argn1
#define buf (char *)udata.u_argn

_stat()
{
	inoptr ino;
	inoptr n_open();

	ifnot (valadr(buf, sizeof(struct stat)) &&
	    (ino = n_open(path, NULLINOPTR)))
		return (-1);

	stcpy(ino, buf);
	i_deref(ino);
	return (0);
}

#undef path
#undef buf

/********************************************
fstat(int16 fd, char *buf)
********************************************/

#define fd (int16)udata.u_argn1
#define buf (char *)udata.u_argn

_fstat()
{
	inoptr ino;
	inoptr getinode();

	ifnot (valadr(buf, sizeof(struct stat)))
		return (-1);

	if ((ino = getinode(fd)) == NULLINODE)
		return (-1);

	stcpy(ino, buf);
	return (0);
}

#undef fd
#undef buf

/* Utility for stat and fstat. */
stcpy(inoptr ino, char *buf)
{
	/* Violently system-dependent. */
	bcopy((char *)&(ino->c_dev), buf, 12);
	bcopy((char *)&(ino->c_node.i_addr[0]), buf + 12, 2);
	bcopy((char *)&(ino->c_node.i_size), buf + 14, 16);
}

/************************************
dup(int16 oldd)
************************************/

#define oldd (uint16)udata.u_argn

_dup()
{
	int newd;
	inoptr getinode();

	if (getinode(oldd) == NULLINODE)
		return (-1);

	if ((newd = uf_alloc()) == -1)
		return (-1);

	udata.u_files[newd] = udata.u_files[oldd];
	++of_tab[udata.u_files[oldd]].o_refs;

	return (newd);
}

#undef oldd

/****************************************
dup2(int16 oldd, int16 newd)
****************************************/

#define oldd (int16)udata.u_argn1
#define newd (int16)udata.u_argn

_dup2()
{
	inoptr getinode();

	if (getinode(oldd) == NULLINODE)
		return (-1);

	if (newd < 0 || newd >= UFTSIZE) {
		udata.u_error = EBADF;
		return (-1);
	}

	ifnot (udata.u_files[newd] & 0x80)
		doclose(newd);

	udata.u_files[newd] = udata.u_files[oldd];
	++of_tab[udata.u_files[oldd]].o_refs;

	return (0);
}

#undef oldd
#undef newd

/**************************************
umask(int mask)
*************************************/

#define mask (int16)udata.u_argn

_umask()
{
	int omask;

	omask = udata.u_mask;
	udata.u_mask = mask & 0777;
	return (omask);
}

#undef mask

/*
 * Special system call returns super-block of given
 * filesystem for users to determine free space, etc.
 * Should be replaced with a sync() followed by a
 * read of block 1 of the device.
 */

/***********************************************
getfsys(int16 dev, struct filesys *buf)
**************************************************/

#define dev (int16)udata.u_argn1
#define buf (struct filesys *)udata.u_argn

_getfsys()
{
	if (dev < 0 || dev >= NDEVS || fs_tab[dev].s_mounted != SMOUNTED) {
		udata.u_error = ENXIO;
		return (-1);
	}

	bcopy((char *)&fs_tab[dev], (char *)buf, sizeof(struct filesys));
	return (0);
}

#undef dev
#undef buf

/****************************************
ioctl(int fd, int request, char *data)
*******************************************/

#define fd (int)udata.u_argn2
#define request (int)udata.u_argn1
#define data (char *)udata.u_argn

_ioctl()
{
	inoptr ino;
	int dev;
	inoptr getinode();

	if ((ino = getinode(fd)) == NULLINODE)
		return (-1);

	ifnot (isdevice(ino)) {
		udata.u_error = ENOTTY;
		return (-1);
	}

	ifnot (getperm(ino) & OTH_WR) {
		udata.u_error = EPERM;
		return (-1);
	}

	dev = ino->c_node.i_addr[0];

	if (d_ioctl(dev, request, data))
		return (-1);
	return (0);
}

#undef fd
#undef request
#undef data

/* This implementation of mount ignores the rwflag. */

/*****************************************
mount(char *spec, char *dir, int rwflag)
*******************************************/

#define spec (char *)udata.u_argn2
#define dir (char *)udata.u_argn1
#define rwflag (int)udata.u_argn

_mount()
{
	inoptr sino, dino;
	int dev;
	inoptr n_open();

	ifnot (super()) {
		udata.u_error = EPERM;
		return (-1);
	}

	ifnot (sino = n_open(spec, NULLINOPTR))
		return (-1);

	ifnot (dino = n_open(dir, NULLINOPTR)) {
		i_deref(sino);
		return (-1);
	}

	if (getmode(sino) != F_BDEV) {
		udata.u_error = ENOTBLK;
		goto nogood;
	}

	if (getmode(dino) != F_DIR) {
		udata.u_error = ENOTDIR;
		goto nogood;
	}

	dev = (int)sino->c_node.i_addr[0];

	if ( dev >= NDEVS || d_open(dev)) {
		udata.u_error = ENXIO;
		goto nogood;
	}

	if (fs_tab[dev].s_mounted || dino->c_refs != 1 ||
	    dino->c_num == ROOTINODE) {
		udata.u_error = EBUSY;
		goto nogood;
	}

	_sync();

	if (fmount(dev, dino)) {
		udata.u_error = EBUSY;
		goto nogood;
	}

	i_deref(dino);
	i_deref(sino);
	return (0);
nogood:
	i_deref(dino);
	i_deref(sino);
	return (-1);
}

#undef spec
#undef dir
#undef rwflag

/******************************************
umount(char *spec)
******************************************/

#define spec (char *)udata.u_argn

_umount()
{
	inoptr sino;
	int dev;
	inoptr ptr;
	inoptr n_open();

	ifnot (super()) {
		udata.u_error = EPERM;
		return (-1);
	}

	ifnot (sino = n_open(spec, NULLINOPTR))
		return (-1);

	if (getmode(sino) != F_BDEV) {
		udata.u_error = ENOTBLK;
		goto nogood;
	}

	dev = (int)sino->c_node.i_addr[0];
	ifnot (validdev(dev)) {
		udata.u_error = ENXIO;
		goto nogood;
	}

	if (!fs_tab[dev].s_mounted) {
		udata.u_error = EINVAL;
		goto nogood;
	}

	for (ptr = i_tab; ptr < i_tab + ITABSIZE; ++ptr) {
		if (ptr->c_refs > 0 && ptr->c_dev == dev) {
			udata.u_error = EBUSY;
			goto nogood;
		}
	}

	_sync();
	fs_tab[dev].s_mounted = 0;
	i_deref(fs_tab[dev].s_mntpt);

	i_deref(sino);
	return (0);
nogood:
	i_deref(sino);
	return (-1);
}

#undef spec
