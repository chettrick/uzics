PROG=	uzics
NOMAN=

SRCS=	data.c filesys.c scall1.c scall2.c
SRCS+=	devio.c devwd.c devmisc.c devtty.c devfd.c
SRCS+=	dispatch.c machdep.c process.c extras.c

.include <bsd.prog.mk>
