#include "all.h"

typedef struct Class_ Class;

enum {
	Cstk = 1, /* pass on the stack */
	Cptr = 2, /* replaced by a pointer */
};

struct Class_ {
	char class;
	uint size;
	uint nreg;
	int reg[7];
};

static int gpreg[] = {R3, R4, R5, R6, R7, R8, R9, R10};
static int fpreg[] = {F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12};

bits
ppc64le_retregs(Ref r, int p[2])
{
	(void)r;
	(void)p;
	assert(0);
}

bits
ppc64le_argregs(Ref r, int p[2])
{
	(void)r;
	(void)p;
	assert(0);
}

static void
argsclass(Ins *i0, Ins *i1, Class *carg, Ref *env)
{
	/* I would just like to take a moment here to comment on how fucking
	 * stupid this ABI is.
	 *
	 * We will assign *every* general purpose register to a single 448-byte
	 * struct and then store the following int parameter on the stack.
	 *
	 * If, for any reason, we pass some arguments on the stack (say, for
	 * example, we ran out of floating point registers), we must reserve the
	 * equivalent number of general purpose registers, in DWORDS. Fucking
	 * WHY?
	 *
	 * We will CUT A STRUCT IN HALF if we run out of registers, put one half
	 * in the registers we have left, and put the other half on the stack.
	 *
	 * Who the fuck wrote this? What's going on? Is this even real? Am I on
	 * acid? If so, THIS IS A BAD TRIP. */
	int ngp, nfp, *gp, *fp;
	Class *c;
	Ins *i;

	gp = gpreg;
	fp = fpreg;
	ngp = 8;
	nfp = 8;
	for (i=i0, c=carg; i<i1; i++, c++)
		switch (i->op) {
		case Opar:
		case Oarg:
			c->size = 8;
			if (KBASE(i->cls) == 0 && ngp > 0) {
				ngp--;
				c->reg[0] = *gp++;
				break;
			}
			if (KBASE(i->cls) == 1 && nfp > 0) {
				nfp--;
				c->reg[0] = *fp++;
				break;
			}
			c->class |= Cstk;
			break;
		case Oparc:
		case Oargc:
			assert(0); // TODO
		case Opare:
			*env = i->to;
			break;
		case Oarge:
			*env = i->arg[0];
			break;
		}
}

static void
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Ins *i;
	Class *ca, *c;
	Ref env;
	int envc;
	uint64_t stk;

	env = R;
	ca = alloc((i1-i0) * sizeof ca[0]);
	argsclass(i0, i1, ca, &env);

	stk = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (c->class & Cptr) {
			assert(0); // TODO
		}
		if (c->class & Cstk)
			stk += c->size;
	}
	stk += stk & 15;

	assert(!stk); // TODO: save area

	assert(fn->retty < 0); // TODO: return value

	envc = !req(R, env);
	assert(!envc); // TODO: environment
}

void
ppc64le_abi(Fn *fn)
{
	Blk *b;
	Ins *i;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	selpar(fn, b->ins, i);
}
