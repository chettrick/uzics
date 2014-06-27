/**************************************************
UZI (Unix Z80 Implementation) Kernel:  devwd.c
***************************************************/

/* XXX - unsigned and static what */
/* XXX - what is the return type */

#include "unix.h"
#include "extern.h"

#define LUN 1
#define RDCMD 0x28
#define WRCMD 0x2a

static char cmdblk[10] = { 0, LUN << 5, 0, 0, 0, 0, 0, 0, 0, 0 }; 

extern char *dptr;
extern int dlen;
extern char *cptr;
extern int busid;
extern scsiop();

wd_read(unsigned minor, int rawflag)
{
	cmdblk[0] = RDCMD;
	if (setup(minor, rawflag))
		return (0);

	chkstat(scsiop(), 1);

	return (cmdblk[8] << 9);
}

wd_write(unsigned minor, int rawflag)
{
	cmdblk[0] = WRCMD;
	if (setup(minor, rawflag))
		return (0);

	chkstat(scsiop(), 0);
	return (cmdblk[8] << 9);
}

static
setup(unsigned minor, int rawflag)
{
	register blkno_t block;

	cptr = cmdblk;
	busid = 1;

	if (rawflag) {
		if (rawflag == 2) {
			cmdblk[8] = swapcnt >> 9;
			dlen = swapcnt;
			dptr = swapbase;
			block = swapblk;
		} else {
			cmdblk[8] = udata.u_count >> 9;
			dlen = udata.u_count;
			dptr = udata.u_base;
			block = udata.u_offset.o_blkno;
		}
	} else {
		cmdblk[8] = 1;
		dlen = 512;
		dptr = udata.u_buf->bf_data;
		block = udata.u_buf->bf_blk;
	}

	block += (minor & 0xff00);
	if (block > (minor << 9)) {
		if (cmdblk[0] == WRCMD)
			udata.u_error = ENXIO;
		return (1);
	}

	cmdblk[5] = block;
	cmdblk[4] = block >> 8;
	return (0);
}

static
chkstat(int stat, int rdflag)
{
	if (stat) {
		kprintf("wd %s failure stat %x", rdflag ? "read": "write", stat);
		panic("");
	}
}

wd_open(int minor)
{
	return (0);
}

/* The following is generic SCSI driver code, also used by devmt.c */
char *dptr;
int dlen;
char *cptr;
int busid;

scsiop()
{
#asm 8080
;
;
OUTIR	MACRO
DB	0EDH
DB	0B3H
ENDM
;
OUTI	MACRO
DB	0EDH
DB	0A3H
ENDM
;
SDATA	EQU	0D8H
SCMD	EQU	0D9H
;
DMAPORT EQU 78H
;
;ENTRY POINT:
;
	PUSH	B
;
LOOP1:	CALL	SWAIT
	JZ	HUNG	;ABORT IF PENDING TRANSACTION

	LDA	busid?	;OUTPUT SCSI BUS ADDRESS
	OUT	SDATA
	MVI	A,1	;SELECT CONTROLLER
	OUT	SCMD	;ASSERT SELECT
..A2:	IN	SCMD	;WAIT FOR BSY TO BE ASSERTED
	ANI	01
	JNZ	..A2
	XRA	A
	OUT	SCMD	;DEASSERT IT
;
	LHLD	cptr?
.LOOP2:	CALL	SWAIT	;WAIT FOR REQ
	JNZ	LOST
	IN	SCMD
	ANI	1FH
	CPI	01100B	;CONTINUE AS LONG AS IT WANTS COMMANDS
	JNZ	ECMD
	ANI	00100B
	JZ	SEQ	;ABORT IF IT HAS A MESSAGE
	MOV	A,M	;TRANSMIT COMMAND
	OUT	SDATA
	INX	H
	JMP	.LOOP2
;
ECMD:	LHLD	dlen?
	MOV	A,H
	ORA	L
	JZ	EDATA	;SKIP DATA I/O IF NECESSARY
	CALL	SWAIT   
	JNZ	LOST
	IN	SCMD	;SEE IF IT REALLY WANTS DATA
	ANI	10H
	JZ	EDATA
	IN	SCMD
	ANI	08H	;CHECK FOR DATA READ OR WRITE
	JNZ	WIO

;FILL IN THE DMA PROGRAM WITH THE CORRECT ADDRESS AND LENGTH
	LHLD	dptr?
	SHLD	RDADR
	LHLD	dlen?
	DCX	H
	SHLD	RDCNT
;
	LXI	H,RDBLK
	MVI	B,RDLEN
	MVI	C,DMAPORT
	OUTIR		;SEND PROGRAM TO DMA CONTROLLER
	JMP	EDATA
;
WIO:
	LHLD	dptr?
	SHLD	WRADR
	LHLD	dlen?
	DCX	H
	SHLD	WRCNT
;
	LXI	H,WRBLK
	MVI	B,WRLEN
	MVI	C,DMAPORT
	OUTIR		;SEND PROGRAM TO DMA CONTROLLER

;
EDATA:
	CALL	SWAIT	;WAIT UNTIL THE CONTROLLER WANTS TO SEND NON-DATA
	JNZ	LOST
	IN	SCMD
	ANI	10H
	JNZ	EDATA
;
;GET STATUS AND SHUT DOWN
;
	MVI	A,0A3H
	OUT	DMAPORT	;TURN OFF DMA CONTROLLER

	IN	SCMD
	ANI	1FH
	CPI	00100B
	JNZ	SEQ	;JUMP IF IT DOESN'T WANT TO SEND STATUS
;
	IN	SDATA
	MOV	L,A
	MVI	H,0
	CALL	SWAIT
	JNZ	LOST
	IN	SCMD
	ANI	1FH
	CPI	00000B
	JNZ	SEQ
	IN	SDATA	;READ FINAL MESSAGE BYTE
;
DONE:	POP	B
	MOV	A,H
	ORA	L
	RET
;
;
LOST:	LXI	H,-1
	JMP	DONE
;
SEQ:	CALL	SWAIT
	LXI	H,-2
	JNZ	DONE
	IN	SDATA	;EAT EXTRA DATA
	JMP	SEQ
;
HUNG:
	CALL	WRESET
	LXI	H,-3
	JMP	DONE
;
;THIS WAITS FOR REQ TO BE ASSERTED OR FOR THE CONNECTION TO BE LOST.
;A NON-ZERO RETURN MEANS CONNECTION WAS LOST.
;
SWAIT:
	IN	SCMD
	ANI	01
	RNZ		;RETURN IF NOT EVEN BUSY
	IN	SCMD
	ANI	22H	;MASK OUT REQ AND MAKE SURE ACK FROM LAST CYCLE IS GONE
	JNZ	SWAIT
	RET
;
WRESET:
	MVI	A,2
	OUT	SCMD
	XTHL
	XTHL
	XTHL
	XTHL
	XRA	A
	OUT	SCMD
	RET
;
;
;THESE ARE THE DMA PROGRAMS FOR READ/WRITE
;
RDBLK:	;PORT A IS THE CONTROLLER, PORT B IS THE MEMORY

	DB	0A3H		;RESET DMA
	DB	01101101B	;WR0
	DB	SDATA
RDCNT:	DS	2
	DB	01101100B	;WR1 
	DB	11001100B	;PORT A CYCLE LENGTH = 4
	DB	01010000B	;WR2
	DB	11001101B	;PORT B CYCLE LENGTH = 3
	DB	11001101B	;WR4 BURST MODE
RDADR:	DS	2
	DB	10001010B	;WR5 READY ACTIVE HIGH
	DB	0CFH		;WR6 LOAD COMMAND
	DB	87H		;WR6 GO COMMAND

RDLEN	EQU $ - RDBLK
;
;
WRBLK:

	DB	0A3H		;RESET DMA
	DB	01101101B	;WR0
	DB	SDATA
WRCNT:	DS	2
	DB	01101100B	;WR1 
	DB	11001101B	;PORT A CYCLE LENGTH = 3 (FOR CONTROLLER)
	DB	01010000B	;WR2
	DB	11001101B	;PORT B CYCLE LENGTH = 3
	DB	11001101B	;WR4 BURST MODE
WRADR:	DS	2
	DB	10001010B	;WR5 READY ACTIVE HIGH
	DB	0CFH		;WR6 LOAD COMMAND
	DB	00000001B	;WR0 (ONLY DIFFERENCE)
	DB	0CFH		;WR6 LOAD COMMAND
	DB	87H		;WR6 GO COMMAND

WRLEN	EQU $ - WRBLK

#endasm
}
