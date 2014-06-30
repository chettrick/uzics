/**************************************************
UZI (Unix Z80 Implementation) Kernel:  data.c
***************************************************/

#include "unix.h"
#define MAIN
#if 0	/* XXX - Comment out temporarily. */
_asm 8080
CSEG
@init?::
	EXTRN	fs@init?
	JMP	fs@init?
_endasm;
#endif
#include "extern.h"
