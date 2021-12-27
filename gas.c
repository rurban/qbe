#include "all.h"


char *gasloc, *gassym;

void
gasemitdat(Dat *d, FILE *f)
{
	static int aligned;
	static char *dtoa[] = {
		[DAlign] = ".balign",
		[DB] = "\t.byte",
		[DH] = "\t.short",
		[DW] = "\t.int",
		[DL] = "\t.quad"
	};
	char *p;

	switch (d->type) {
	case DStart:
		aligned = 0;
		if (d->u.sec.name && d->u.sec.flags) {
			fprintf(f, ".section %s, %s\n", d->u.sec.name, d->u.sec.flags);
		} else if (d->u.sec.name) {
			fprintf(f, ".section %s\n", d->u.sec.name);
		} else {
			fprintf(f, ".data\n");
		}
		break;
	case DEnd:
		break;
	case DName:
		if (!aligned)
			fprintf(f, ".balign 8\n");
		p = d->u.str[0] == '"' ? "" : gassym;
		if (d->export)
			fprintf(f, ".globl %s%s\n", p, d->u.str);
		fprintf(f, "%s%s:\n", p, d->u.str);
		break;
	case DZ:
		fprintf(f, "\t.fill %"PRId64",1,0\n", d->u.num);
		break;
	default:
		if (d->type == DAlign)
			aligned = 1;

		if (d->isstr) {
			if (d->type != DB)
				err("strings only supported for 'b' currently");
			fprintf(f, "\t.ascii %s\n", d->u.str);
		}
		else if (d->isref) {
			p = d->u.ref.name[0] == '"' ? "" : gassym;
			fprintf(f, "%s %s%s%+"PRId64"\n",
				dtoa[d->type], p, d->u.ref.name,
				d->u.ref.off);
		}
		else {
			fprintf(f, "%s %"PRId64"\n",
				dtoa[d->type], d->u.num);
		}
		break;
	}
}

typedef struct Asmbits Asmbits;

struct Asmbits {
	char bits[16];
	int size;
	Asmbits *link;
};

static Asmbits *stash;

int
gasstash(void *bits, int size)
{
	Asmbits **pb, *b;
	int i;

	assert(size == 4 || size == 8 || size == 16);
	for (pb=&stash, i=0; (b=*pb); pb=&b->link, i++)
		if (size <= b->size)
		if (memcmp(bits, b->bits, size) == 0)
			return i;
	b = emalloc(sizeof *b);
	memcpy(b->bits, bits, size);
	b->size = size;
	b->link = 0;
	*pb = b;
	return i;
}

void
gasemitfin(FILE *f)
{
	Asmbits *b;
	char *p;
	int sz, i;
	double d;

	if (!stash)
		return;
	fprintf(f, "/* floating point constants */\n.data\n");
	for (sz=16; sz>=4; sz/=2)
		for (b=stash, i=0; b; b=b->link, i++) {
			if (b->size == sz) {
				fprintf(f,
					".balign %d\n"
					"%sfp%d:",
					sz, gasloc, i
				);
				for (p=b->bits; p<&b->bits[sz]; p+=4)
					fprintf(f, "\n\t.int %"PRId32,
						*(int32_t *)p);
				if (sz <= 8) {
					if (sz == 4)
						d = *(float *)b->bits;
					else
						d = *(double *)b->bits;
					fprintf(f, " /* %f */\n", d);
				} else
					fprintf(f, "\n");
			}
		}
	while ((b=stash)) {
		stash = b->link;
		free(b);
	}
}
