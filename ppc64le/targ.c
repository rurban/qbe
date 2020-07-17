#include "all.h"

static int
ppc64le_memargs(int op)
{
	(void)op;
	return 0;
}

Target T_ppc64le = {
	.gpr0 = R3,
	.ngpr = NGPR,
	.fpr0 = F0,
	.nfpr = NFPR,
	.rglob = BIT(R0) | BIT(R1) | BIT(R2) | BIT(R12) | BIT(R13) | BIT(R31),
	.nrglob = 6,
	.rsave = ppc64le_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = ppc64le_retregs,
	.argregs = ppc64le_argregs,
	.memargs = ppc64le_memargs,
	.abi = ppc64le_abi,
	.isel = ppc64le_isel,
	.emitfn = ppc64le_emitfn,
};

int ppc64le_rsave[] = {
	R3, R4, R5, R6, R7, R8, R9, R10, R11,
	F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, -1
};
int ppc64le_rclob[] = {
	R14, R15, R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27,
	R28, R29, R30, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31,
	-1
};

MAKESURE(ppc64le_arrays_ok,
	sizeof ppc64le_rsave == (NGPS+NFPS+1) * sizeof(int) &&
	sizeof ppc64le_rclob == (NCLR+1) * sizeof(int)
);
