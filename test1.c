#include <stdio.h>
#include <dos.h>


#define	HPtoL(p)  (((unsigned long)FP_SEG(p)<<4) + (unsigned long)FP_OFF(p))

char far   *HPtoFP(n)
char huge  *n;
{
	register unsigned  s, o;
	unsigned	d;
	char	far	*p;

	o = FP_OFF(n);
	s = FP_SEG(n);
	if (o >	16)  {
		d = o &	~0xf;
		o -= d;
		s += (d>>4);
	}
	FP_OFF(p) = o;
	FP_SEG(p) = s;
	return(p);
}


main(argc, argv)
int	argc;
char	*argv[];
{
	union  {
		struct	{
			unsigned short	off;
			unsigned short	seg;
		}	v;
		char huge *h;
		char far  *f;
		unsigned long	l;
	}	x, y, t;
	char	*p;

	if (argc != 5)  {
		fprintf(stderr, "Usage: test seg1 off1 seg2 off2\n");
		exit(-1);
	}
	x.v.seg = atoi(argv[1]);
	x.v.off = atoi(argv[2]);
	y.v.seg = atoi(argv[3]);
	y.v.off = atoi(argv[4]);
	printf("x = %u:%u, long = %lu, (long) = %lu, HPtoL = %lu\n",
	       x.v.seg, x.v.off, x.l, (long) x.h, HPtoL(x.h));
	printf("y = %u:%u, long = %lu, (long) = %lu, HPtoL = %lu\n",
	       y.v.seg, y.v.off, y.l, (long) y.h, HPtoL(y.h));
	printf("!F1 = %d, !F2 = %d, !H1 = %d, !H2 = %d\n", !x.f, !y.f, !x.h, !y.h);
	printf("F1 == F2 is %d\n", x.f == y.f);
	printf("H1 == H2 is %d\n", x.h == y.h);
	printf("F1 != F2 is %d\n", x.f != y.f);
	printf("H1 != H2 is %d\n", x.h != y.h);
	printf("F1 < F2 is %d\n", x.f < y.f);
	printf("F1 - F2 is %d\n", x.f - y.f);
	printf("H1 - H2 is %d\n", x.h - y.h);
	t.l = x.l;
	t.f += 1;
	printf("F1 + 1 is %u:%u (%lu)\n", t.v.seg, t.v.off, HPtoL(t.h));
	t.l = x.l;
	t.h += 1;
	printf("H1 + 1 is %u:%u (%lu)\n", t.v.seg, t.v.off, HPtoL(t.h));
}

