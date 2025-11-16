#include <stdio.h>
#include "vmem.h"

#define	MAX_ITEMS	50
#define	MAX_SIZE	100000L
#define	MAX_ALLOC	10000000L


#ifndef _MSC_VER
#include <stdlib.h>
#else
#endif

static void tsterr(char *m);


int main(int argc, char *argv[])
{
	static unsigned	short	v[MAX_ITEMS];
	static unsigned	long	len[MAX_ITEMS];
	static unsigned	char	val[MAX_ITEMS];
	static char		fv[MAX_ITEMS];
	long	a = 0L;
	register long	 j;
	register unsigned char	c;
	int	i, max = 0, n, mpass, nfrez = 0;
	unsigned char	 *p;

	if (argc > 1) {
		max = atoi(argv[1]);
		max = max < 1 ? 1 : max;
		max = max > MAX_ITEMS ? MAX_ITEMS : max;
	}  else
		max = MAX_ITEMS;
	VM_init();
	/* Set max real memory to 20MB, allocation chunk to 1MB */
	VM_parm(20000000L, 1000000L, -1.0, -1L, -1, -1);
	if (argc > 2)	
		mpass = atoi(argv[2]);
	else	
		mpass = max;
	for (i=0 ; i != max ; ++i)  {
		len[i] = (1 + rand()) % (MAX_SIZE-1L);
		if (!(v[i] = VM_alloc((long) len[i], 0)))
			tsterr("VM_alloc returned 0");
		nfrez += fv[i] = rand() % 100 == 50;
		if (!(p = VM_addr(v[i], 1, fv[i])))
			tsterr("vm_addr returned NULL");
		a += len[i];
		val[i] = c = rand() % 256;
		for (j=len[i] ; j-- ; )
			*p++ = c;
		printf("%lu bytes allocated - total %lu, nfrez = %d.\n", len[i], a, nfrez);
		if (a == 197353L)
			i = i;
	}

	if (mpass)
		for (i=0 ; i != max ; ++i)  {
			if (!(p = VM_addr(v[i], 0, fv[i])))
				tsterr("vm_addr returned NULL");
			c = val[i];
			for (j=len[i] ; j-- ; )
				if (*p++ != c)
					tsterr("not equal");
			printf("%d OK\n", i);
		}
	for (n=0 ; n != mpass ; ++n)  {
		i = rand() % max;
		VM_free(v[i]);
		len[i] = 1 + rand() % (MAX_SIZE-1);
		if (!(v[i] = VM_alloc((long) len[i], 0)))
			tsterr("VM_alloc returned 0");
		if (!(p = VM_addr(v[i], 1, fv[i])))
			tsterr("vm_addr returned NULL");
		c = val[i] = rand() % 256;
		for (j=len[i] ; j-- ; )
			*p++ = c;
		printf("pass %d of %d, i = %d\n", n, mpass, i);
	}
	for (i=0 ; i !=	max ; ++i)  {
		if (!(p = VM_addr(v[i], 0, fv[i])))
			tsterr("vm_addr returned NULL");
		c = val[i];
		for (j=len[i] ; j-- ; )
			if (*p++ != c)
				tsterr("not equal");
		printf("%d OK\n", i);
	}

	printf("now dumping...\n");
	VM_dump("dump");
	printf("now restoring...\n");
	VM_rest("dump");
	printf("now fast restoring...\n");
	VM_frest("dump");
	printf("done\n");
	for (i=0 ; i != max ; ++i)  {
		if (!(p = VM_addr(v[i], 0, 0)))
			tsterr("vm_addr returned NULL");
		c = val[i];
		for (j=len[i] ;	j-- ; )
			if (*p++ != c)	tsterr("not equal");
		printf("%d OK\n", i);
	}

	VM_fcore();
	return 0;
}

static void tsterr(char *m)
{
	fprintf(stderr, "%s\n", m);
	exit(1);
}

