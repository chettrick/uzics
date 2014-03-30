/**************************************************
UZI (Unix Z80 Implementation) Kernel:  devflop.c
***************************************************/


#include "unix.h"
#include "extern.h"

extern ei();


#define NUMTRKS 76
#define NUMSECS 26

static char ftrack, fsector, ferror;
static char *fbuf;

static read(), write(), reset();


fd_read(minor, rawflag)
int16 minor;
int rawflag;
{
    return (fd(1, minor, rawflag));
}

fd_write(minor, rawflag)
int16 minor;
int rawflag;
{
    return (fd(0, minor, rawflag));
}



static
fd(rwflag, minor, rawflag)
int rwflag;
int minor;
int rawflag;
{
    register unsigned nblocks;
    register unsigned firstblk;

    if (rawflag)
    {
        if (rawflag == 2)
        {
            nblocks = swapcnt >> 7;
            fbuf = swapbase;
            firstblk = 4*swapblk;
        }
        else
        {
            nblocks = udata.u_count >> 7;
            fbuf = udata.u_base;
            firstblk = udata.u_offset.o_blkno * 4;
        }       
    }
    else
    {
        nblocks = 4;
        fbuf = udata.u_buf->bf_data;
        firstblk = udata.u_buf->bf_blk * 4;
    }

    ftrack = firstblk / 26; 
    fsector = firstblk % 26 + 1;
    ferror = 0;

    for (;;)
    {
        if (rwflag)
            read();
        else
            write();

        ifnot (--nblocks)
            break;

        if (++fsector == 27)
        {
            fsector = 1;
            ++ftrack;
        }
        fbuf += 128;
    }

    if (ferror)
    {
        kprintf("fd_%s: error %d track %d sector %d\n",
                    rwflag?"read":"write", ferror, ftrack, fsector);
        panic("");
    }

    return(nblocks);
}


fd_open(minor)
int minor;
{
    if (in(0x80) & 0x81)
    {
        udata.u_error = ENXIO;
        return (-1);
    }
    reset();
    return(0);
}


fd_close(minor)
int minor;
{
    return(0);
}


fd_ioctl(minor)
int minor;
{
    return(-1);
}



#asm 8080
; ALL THE FUNCTIONS IN HERE ARE STATIC TO THIS PACKAGE

;
;THESE ARE 1771 FLOPPY DISK CONTROLLER COMMANDS,
;I/O PORT ADDRESSES, AND FLAG MASKS:
;
RESTOR  EQU     08H     ;6MS STEP,LOAD HEAD
SEEK    EQU     18H     ;6MS STEP,LOAD HEAD
READC   EQU     88H     ;NO HEAD LOAD
WRITEC  EQU     0A8H    ;NO HEAD LOAD
RESET   EQU     0D0H    ;RESET STATUS COMMAND
STATUS  EQU     80H
COMAND  EQU     80H
TRACK   EQU     81H
SECTOR  EQU     82H
DATA    EQU     83H
BUSY    EQU     01
RDMASK  EQU     10011111B
WRMASK  EQU     0FFH
;
;
;THIS FLOPPY READ ROUTINE CALLS FREAD2. IF THE
;READ IS UNSUCCESSFUL, THE DRIVE IS HOMED,
;AND THE READ IS RETRIED.
;
read?:  PUSH    B
        CALL    FREAD2
        LDA     ferror?
        ANA     A       ;SET FLAGS
        JZ      RDONE   ;READ WAS OK
        CALL    FHOME
        CALL    FREAD2
RDONE:
        POP     B
        RET
;
FREAD2: CALL    FWAIT
        LDA     fsector?
        OUT     SECTOR
        CALL    FTKSET
        MVI     A,11
FREAD3: STA     TRYNUM
        CALL    FWAIT
        LHLD    fbuf?
        MVI     C,DATA
        MVI     B,128
        MVI     D,03
        DI              ;ENTERING CRITICAL SECTION
        MVI     A,READC
        OUT     COMAND
        XTHL    ;SHORT DELAY
        XTHL
        XTHL
        XTHL
.1LOOP: IN      STATUS
        ANA     D
        DCR     A
        JZ      .1LOOP
.Z80
        INI
.8080
        JNZ     .1LOOP
        CALL    ei?             ;LEAVING CRITICAL SECTION
        CALL    FWAIT
        IN      STATUS
        ANI     RDMASK
        JZ      FR1END
        LDA     TRYNUM
        DCR     A
        JNZ     FREAD3
        MVI     A,1
FR1END: STA     ferror?
        RET
;
;THIS IS THE FLOPPY WRITE ROUTINE. IT CALLS
;THE ACTUAL WRITING SUBROUTINE, AND IF IT
;WAS UNSUCCESSFUL, RESETS THE HEAD AND
;TRIES AGAIN.
;
write?: PUSH    B
        CALL    FWR2
        LDA     ferror?
        ANA     A
        JZ      WDONE
        CALL    FHOME
        CALL    FWR2
        LDA     ferror?
WDONE:  POP     B
        RET
;
FWR2:   CALL    FWAIT
        LDA     fsector?
        OUT     SECTOR
        XTHL
        XTHL
        MVI     A,RESET
        OUT     COMAND
        XTHL
        XTHL
        XTHL
        XTHL
        IN      STATUS
        ANI     00100000B
        JZ      FWR5    ;JMP IF HEAD NOT LOADED
        LDA     ftrack? ;DESIRED TRACK
        MOV     C,A
        IN      TRACK   ;ACTUAL TRACK
        CMP     C
        JZ      FWR4    ;IF TRACK CORRECT
        CALL    FTKSET
        CALL    FWAIT
        LXI     H,30    ;15MS DELAY
        CALL    DELAY
        JMP     FWR4
FWR5:   CALL    FTKSET
        LXI     H,50    ;25 MS DELAY
        CALL    DELAY
        CALL    FWAIT
        LXI     H,20    ;15 MS DELAY
        CALL    DELAY
FWR4:   MVI     A,11
FWR3:   STA     TRYNUM
        CALL    FWAIT
        LHLD    fbuf?
        MVI     C,DATA
        MVI     B,128
        MVI     D,01
        MVI     A,WRITEC
        DI              ;ENTERING CRITICAL SECTION
        OUT     COMAND
.1A:    IN      STATUS
        ANI     01
        JZ      .1A
.1B:    IN      STATUS
        ANI     02
        JNZ     .2LOOP
        IN      STATUS
        ANI     01
        JZ      .2ERROR
        JMP     .1B
.2LOOP: IN      STATUS
        XRA     D
        JZ      .2LOOP
.Z80
        OUTI
.8080
        JNZ     .2LOOP
        CALL    ei?             ;LEAVING CRITICAL SECTION
        CALL    FWAIT
        IN      STATUS
        ANI     WRMASK
        JZ      FW2END
.2ERROR: LDA    TRYNUM
        DCR     A
        JNZ     FWR3
        MVI     A,1
FW2END: STA     ferror?
        RET
;
;THESE 3 SUBROUTINES ARE USED BY THE FREAD2 AND
;FWR2 ROUTINES:
;
FTKSET: CALL    FWAIT
        LDA     ftrack?
        OUT     DATA
        MVI     A,SEEK
        OUT     COMAND
        RET
;
FWAIT:  IN      STATUS
        ANI     10000001B
        JNZ     FWAIT
        RET
;
reset?:
FHOME:  PUSH    B
        CALL    FWAIT
        MVI     A,RESTOR
        OUT     COMAND
        POP     B
        RET
;
        

;THIS IS USED IN SEVERAL PLACES. IT GIVES
; ( .5 * HL ) MILLISECONDS OF DELAY.
;
DELAY:  MVI     B,154
.Z80
DELAY1: DJNZ    DELAY1  ;.5 MS DELAY
.8080
        DCX     H
        MOV     A,H
        ORA     L
        JNZ     DELAY   ;LOOP HL TIMES
        RET

TRYNUM: DS      1
#endasm

