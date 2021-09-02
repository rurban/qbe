#include "all.h"

typedef struct Class Class;
typedef struct Insl Insl;
typedef struct Params Params;

enum {
	Cptr  = 1, /* replaced by a pointer */
	Cstk1 = 2, /* pass first XLEN on the stack */
	Cstk2 = 4, /* pass second XLEN on the stack */
	Cstk = Cstk1 | Cstk2,
};

struct Class {
	char class;
	uint size;
	Typ *t;
	uchar nreg;
	uchar ngp;
	uchar nfp;
	int reg[2];
	int cls[2];
};

struct Insl {
	Ins i;
	Insl *link;
};

struct Params {
	uint ngp;
	uint nfp;
	uint stk; /* stack offset for varargs */
};

static int gpreg[] = { A0,  A1,  A2,  A3,  A4,  A5,  A6,  A7};
static int fpreg[] = {FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7};

/* layout of call's second argument (RCall)
 *
 *  29   12    8    4  2  0
 *  |0.00|x|xxxx|xxxx|xx|xx|                  range
 *        |    |    |  |  ` gp regs returned (0..2)
 *        |    |    |  ` fp regs returned    (0..2)
 *        |    |    ` gp regs passed         (0..8)
 *        |     ` fp regs passed             (0..8)
 *        ` is x8 used                       (0..1)
 */

bits
rv64_retregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp;

	assert(rtype(r) == RCall);
	ngp = r.val & 3;
	nfp = (r.val >> 2) & 3;
	if (p) {
		p[0] = ngp;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(A0+ngp);
	while (nfp--)
		b |= BIT(FA0+nfp);
	return b;
}

bits
rv64_argregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp;

	assert(rtype(r) == RCall);
	ngp = (r.val >> 4) & 15;
	nfp = (r.val >> 8) & 15;
	b = 0;
	if (p) {
		p[0] = ngp;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(A0+ngp);
	while (nfp--)
		b |= BIT(FA0+nfp);
	return b;
}

static void
typclass(Class *c, Typ *t, int *gp, int *fp)
{
	uint64_t sz;
	uint n;

	sz = (t->size + 7) & ~7;
	c->t = t;
	c->class = 0;
	c->ngp = 0;
	c->nfp = 0;

	if (t->align > 4)
		err("alignments larger than 16 are not supported");

	if (t->dark || sz > 16 || sz == 0) {
		/* large structs are replaced by a
		 * pointer to some caller-allocated
		 * memory */
		c->class |= Cptr;
		c->size = 8;
		return;
	}

	/* TODO: float */

	for (n=0; n<sz/8; n++, c->ngp++) {
		c->reg[n] = *gp++;
		c->cls[n] = Kl;
	}

	c->nreg = n;
}

static void
sttmps(Ref tmp[], int cls[], uint nreg, Ref mem, Fn *fn)
{
	static int st[] = {
		[Kw] = Ostorew, [Kl] = Ostorel,
		[Ks] = Ostores, [Kd] = Ostored
	};
	uint n;
	uint64_t off;
	Ref r;

	assert(nreg <= 4);
	off = 0;
	for (n=0; n<nreg; n++) {
		tmp[n] = newtmp("abi", cls[n], fn);
		r = newtmp("abi", Kl, fn);
		emit(st[cls[n]], 0, R, tmp[n], r);
		emit(Oadd, Kl, r, mem, getcon(off, fn));
		off += KWIDE(cls[n]) ? 8 : 4;
	}
}

static void
ldregs(int reg[], int cls[], int n, Ref mem, Fn *fn)
{
	int i;
	uint64_t off;
	Ref r;

	off = 0;
	for (i=0; i<n; i++) {
		r = newtmp("abi", Kl, fn);
		emit(Oload, cls[i], TMP(reg[i]), r, R);
		emit(Oadd, Kl, r, mem, getcon(off, fn));
		off += KWIDE(cls[i]) ? 8 : 4;
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j, k, cty;
	Ref r;

	j = b->jmp.type;

	if (!isret(j) || j == Jret0)
		return;

	r = b->jmp.arg;
	b->jmp.type = Jret0;

	if (j == Jretc) {
		die("unimplemented");
	} else {
		k = j - Jretw;
		if (KBASE(k) == 0) {
			emit(Ocopy, k, TMP(A0), r, R);
			cty = 1;
		} else {
			emit(Ocopy, k, TMP(FA0), r, R);
			cty = 1 << 2;
		}
	}

	b->jmp.arg = CALL(cty);
}

static int
argsclass(Ins *i0, Ins *i1, Class *carg, Ref *env)
{
	int k, ngp, nfp, *gp, *fp, vararg;
	Class *c;
	Ins *i;

	gp = gpreg;
	fp = fpreg;
	ngp = 8;
	nfp = 8;
	vararg = 0;
	for (i=i0, c=carg; i<i1; i++, c++) {
		switch (i->op) {
		case Opar:
		case Oarg:
			c->cls[0] = i->cls;
			c->size = 8;
			/* variadic float args are passed in int regs */
			k = !vararg ? i->cls : KWIDE(i->cls) ? Kl : Kw;
			if (KBASE(k) == 0 && ngp > 0) {
				ngp--;
				c->reg[0] = *gp++;
			} else if (KBASE(k) == 1 && nfp > 0) {
				nfp--;
				c->reg[0] = *fp++;
			} else {
				c->class |= Cstk1;
			}
			break;
		case Oargv:
			/* subsequent arguments are variadic */
			vararg = 1;
			break;
		case Oparc:
		case Oargc:
			typclass(c, &typ[i->arg[0].val], gp, fp);
			if (c->class & Cptr) {
				c->ngp = 1;
				c->reg[0] = *gp;
				c->cls[0] = Kl;
			}
			if (c->ngp <= ngp && c->nfp <= nfp) {
				ngp -= c->ngp;
				nfp -= c->nfp;
				gp += c->ngp;
				fp += c->nfp;
				break;
			}
			c->ngp += c->nfp;
			c->nfp = 0;
			if (c->ngp <= ngp) {
				ngp -= c->ngp;
				gp += c->ngp;
				break;
			}
			c->class |= Cstk1;
			if (c->ngp - 1 > ngp)
				c->class |= Cstk2;
			break;
		case Opare:
			*env = i->to;
			break;
		case Oarge:
			*env = i->arg[0];
			break;
		}
	}
	return (gp-gpreg) << 4 | (fp-fpreg) << 8;
}

static void
stkblob(Ref r, Class *c, Fn *fn, Insl **ilp)
{
	Insl *il;
	int al;

	il = alloc(sizeof *il);
	al = c->t->align - 2; /* NAlign == 3 */
	if (al < 0)
		al = 0;
	il->i = (Ins){Oalloc+al, Kl, r, {getcon(c->t->size, fn)}};
	il->link = *ilp;
	*ilp = il;
}

static void
selcall(Fn *fn, Ins *i0, Ins *i1, Insl **ilp)
{
	Ins *i;
	Class *ca, *c, cr;
	int k, cty, envc, vararg;
	uint64_t stk, off;
	Ref r, env;

	env = R;
	ca = alloc((i1-i0) * sizeof ca[0]);
	cty = argsclass(i0, i1, ca, &env);

	stk = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op == Oargv)
			continue;
		if (c->class & Cptr) {
			i->arg[0] = newtmp("abi", Kl, fn);
			stkblob(i->arg[0], c, fn, ilp);
			i->op = Oarg;
		}
		if (c->class & Cstk1)
			stk += 8;
		if (c->class & Cstk2)
			stk += 8;
	}
	if (stk)
		emit(Oadd, Kl, TMP(SP), TMP(SP), getcon(stk, fn));

	if (!req(i1->arg[1], R)) {
		typclass(&cr, &typ[i1->arg[1].val], gpreg, fpreg);
		abort();  /* XXX: implement */
	} else if (KBASE(i1->cls) == 0) {
		emit(Ocopy, i1->cls, i1->to, TMP(A0), R);
		cty |= 1;
	} else {
		emit(Ocopy, i1->cls, i1->to, TMP(FA0), R);
		cty |= 1 << 2;
	}

	envc = !req(R, env);
	if (envc)
		die("todo (rv64 abi): env calls");
	emit(Ocall, 0, R, i1->arg[0], CALL(cty));

	/* move arguments into registers */
	vararg = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op == Oargv) {
			vararg = 1;
			continue;
		}
		if (c->class & Cstk1)
			continue;
		if (i->op == Oargc) {
			ldregs(c->reg, c->cls, c->nreg, i->arg[1], fn);
		} else if (vararg && KBASE(*c->cls) == 1) {
			k = KWIDE(*c->cls) ? Kl : Kw;
			r = newtmp("abi", k, fn);
			emit(Ocopy, k, TMP(c->reg[0]), r, R);
			c->reg[0] = r.val;
		} else {
			emit(Ocopy, *c->cls, TMP(*c->reg), i->arg[0], R);
		}
	}

	off = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op == Oargv || (c->class & Cstk) == 0)
			continue;
		if (i->op != Oargc) {
			r = newtmp("abi", Kl, fn);
			emit(Ostorel, 0, R, i->arg[0], r);
			emit(Oadd, Kl, r, TMP(SP), getcon(off, fn));
		} else
			blit(TMP(SP), off, i->arg[1], c->size, fn);
		off += c->size;
	}

	if (vararg) {
		vararg = 0;
		for (i=i0, c=ca; i<i1; i++, c++) {
			if (i->op == Oargv)
				vararg = 1;
			else if (vararg && KBASE(*c->cls) == 1)
				emit(Ocast, KWIDE(*c->cls) ? Kl : Kw, TMP(*c->reg), i->arg[0], R);
		}
	}

	if (stk)
		emit(Osub, Kl, TMP(SP), TMP(SP), getcon(stk, fn));

	for (i=i0, c=ca; i<i1; i++, c++)
		if (i->op != Oargv && c->class & Cptr)
			blit(i->arg[0], 0, i->arg[1], c->t->size, fn);
}

static Params
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Class *ca, *c, cr;
	Insl *il;
	Ins *i;
	int n, s, cty;
	Ref r, env, tmp[16], *t;

	env = R;
	ca = alloc((i1-i0) * sizeof ca[0]);
	curi = &insb[NIns];

	cty = argsclass(i0, i1, ca, &env);
	fn->reg = rv64_argregs(CALL(cty), 0);

	il = 0;
	t = tmp;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op != Oparc || (c->class & (Cptr|Cstk)))
			continue;
		sttmps(t, c->cls, c->nreg, i->to, fn);
		stkblob(i->to, c, fn, &il);
		t += c->nreg;
	}
	for (; il; il=il->link)
		emiti(il->i);

	if (fn->retty >= 0) {
		typclass(&cr, &typ[fn->retty], gpreg, fpreg);
		if (cr.class & Cptr) {
			fn->retr = newtmp("abi", Kl, fn);
			emit(Ocopy, Kl, fn->retr, TMP(A0), R);
		}
	}

	t = tmp;
	for (i=i0, c=ca, s=2 + 8 * fn->vararg; i<i1; i++, c++) {
		if (i->op == Oparc
		&& (c->class & Cptr) == 0) {
			if (c->class & Cstk) {
				fn->tmp[i->to.val].slot = -s;
				s += c->size / 8;
			} else {
				for (n=0; n<c->nreg; n++) {
					r = TMP(c->reg[n]);
					emit(Ocopy, c->cls[n], *t++, r, R);
				}
			}
		} else if (c->class & Cstk1) {
			r = newtmp("abi", Kl, fn);
			emit(Oload, c->cls[0], i->to, r, R);
			emit(Oaddr, Kl, r, SLOT(-s), R);
			s++;
		} else {
			emit(Ocopy, c->cls[0], i->to, TMP(c->reg[0]), R);
		}
	}

	if (!req(R, env))
		die("todo (arm abi): env calls");

	return (Params){
		.stk = s,
		.ngp = (cty >> 4) & 15,
		.nfp = (cty >> 8) & 15,
	};
}

static void
selvaarg(Fn *fn, Blk *b, Ins *i)
{
	Ref loc, newloc;

	loc = newtmp("abi", Kl, fn);
	newloc = newtmp("abi", Kl, fn);
	emit(Ostorel, Kw, R, newloc, i->arg[0]);
	emit(Oadd, Kl, newloc, loc, getcon(8, fn));
	emit(Oload, i->cls, i->to, loc, R);
	emit(Oload, Kl, loc, i->arg[0], R);
}

static void
selvastart(Fn *fn, Params p, Ref ap)
{
	Ref rsave;
	int s;

	rsave = newtmp("abi", Kl, fn);
	emit(Ostorel, Kw, R, rsave, ap);
	s = p.stk > 2 + 8 * fn->vararg ? p.stk : 2 + p.ngp;
	emit(Oaddr, Kl, rsave, SLOT(-s), R);
}

void
rv64_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0, *ip;
	Insl *il;
	int n;
	Params p;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	p = selpar(fn, b->ins, i);
	n = b->nins - (i - b->ins) + (&insb[NIns] - curi);
	i0 = alloc(n * sizeof(Ins));
	ip = icpy(ip = i0, curi, &insb[NIns] - curi);
	ip = icpy(ip, i, &b->ins[b->nins] - i);
	b->nins = n;
	b->ins = i0;

	/* lower calls, returns, and vararg instructions */
	il = 0;
	b = fn->start;
	do {
		if (!(b = b->link))
			b = fn->start; /* do it last */
		if (b->visit)
			continue;
		curi = &insb[NIns];
		selret(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;)
			switch ((--i)->op) {
			default:
				emiti(*i);
				break;
			case Ocall:
			case Ovacall:
				for (i0=i; i0>b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;
				selcall(fn, i0, i, &il);
				i = i0;
				break;
			case Ovastart:
				selvastart(fn, p, i->arg[0]);
				break;
			case Ovaarg:
				selvaarg(fn, b, i);
				break;
			case Oarg:
			case Oargc:
				die("unreachable");
			}
		if (b == fn->start)
			for (; il; il=il->link)
				emiti(il->i);
		b->nins = &insb[NIns] - curi;
		idup(&b->ins, curi, b->nins);
	} while (b != fn->start);

	if (debug['A']) {
		fprintf(stderr, "\n> After ABI lowering:\n");
		printfn(fn, stderr);
	}
}
