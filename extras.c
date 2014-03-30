/**************************************************
UZI (Unix Z80 Implementation) Kernel:  extras.c
***************************************************/


bcopy()
{
#asm 8080
; BCOPY(SRC,DEST,COUNT)
;
        POP     H
        SHLD    HOLDER
.Z80
        LD      (BCHLDR),BC
.8080   
        POP     B
        POP     D
        POP     H
        PUSH    H
        PUSH    H
        PUSH    H
.Z80
        LDIR
.8080
        LHLD    HOLDER
.Z80
        LD      BC,(BCHLDR)
.8080
        PCHL
#endasm
}

#asm 
;
HOLDER: DS      2
BCHLDR: DS      2
;
;
#endasm


bzero(ptr,count)
char *ptr;
int count;
{
    *ptr = 0;
    bcopy(ptr,ptr+1,count-1);
}


abort()
{
#asm 8080
        DI
        JMP     $
#endasm
}

