/**************************************************
UZI (Unix Z80 Implementation) Kernel:  data.c
***************************************************/

#include "unix.h"
#define MAIN
#asm 8080
CSEG
@init?::
        EXTRN   fs@init?
        JMP     fs@init?
#endasm
#include "extern.h"

