#include "../all.h"

typedef struct Ppc64leOp Ppc64leOp;

enum Ppc64leReg {
	R3 = RXX+1, /* caller-save */
	R4,
	R5,
	R6,
	R7,
	R8,
	R9,
	R10,
	R11, /* env */

	R14, /* callee-save */
	R15,
	R16,
	R17,
	R18,
	R19,
	R20,
	R21,
	R22,
	R23,
	R24,
	R25,
	R26,
	R27,
	R28,
	R29,
	R30,

	R0, /* globally live */
	R1,
	R2,
	R31,
	R12,
	R13,
	LR,

	F0, /* caller-save */
	F1,
	F2,
	F3,
	F4,
	F5,
	F6,
	F7,
	F8,
	F9,
	F10,
	F11,
	F12,
	F21, /* callee-save */
	F22,
	F23,
	F24,
	F25,
	F26,
	F27,
	F28,
	F29,
	F30,
	F31,

	NFPR = F31 - F0 + 1,
	NGPR = R13 - R3 + 1,
	NGPS = R11 - R3 + 1,
	NFPS = F12 - F0 + 1,
	NCLR = (R30 - R14 + 1) + (F31 - F21 + 1),
};
MAKESURE(reg_not_tmp, F31 < (int)Tmp0);

/* targ.c */
extern int ppc64le_rsave[];
extern int ppc64le_rclob[];

/* abi.c */
bits ppc64le_retregs(Ref, int[2]);
bits ppc64le_argregs(Ref, int[2]);
void ppc64le_abi(Fn *);

/* isel.c */
void ppc64le_isel(Fn *);

/* emit.c */
void ppc64le_emitfn(Fn *, FILE *);
