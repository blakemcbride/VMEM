/*
 *
 *	       Copyright (c) 1988  Blake McBride
 *	       All Rights Reserved.
 *
 *
 *	       Blake McBride
 *	       4535 Scenic Hills Drive
 *	       Franklin,  TN  37067
 *
 *             blake@mcbride.name
 *
 *
 *	 	See the "License.txt" file for usage terms.
*/


/*
 *
 *		Initial	Release	- Version 1.0 -	March 21, 1988
 *
 *		Version	2.0 -  April 9,	1988
 *
 *			Added the ability to freeze an object in real memory.
 *
 *			Documented the entire system.
 *
 *			Syntax of VM_addr() changed.
 *
 *			Returned value of VM_stat() changed.
 *
 *			Argument to VM_alloc() and VM_realloc()	changed.
 *
 *			Improved error recovery	on disk	full errors.
 *
 *			Fixed bug in compact() which made it miss compacting
 *			memory under curtin conditions.
 *
 *			Fixed a	few places where compact could mess things up.
 *
 *		Version	3.0 -  4/24/88
 *
 *			Fixed bug in vmget_head()
 *
 *			Added some type	casts to keep the compiler (MS-C 5.1)
 *			happy.
 *
 *			Added ability to clear memory when VM_alloc, therefor,
 *			changing its syntax.
 *
 *		Version 3.1
 *
 *			fixed minor bug in VM_realloc
 *
 *		Version 3.2 -  11/4/88
 *
 *			Real memory is now allocated in blocks instead of
 *			the exact amount needed over a minimum.
 *
 *			Fixed a major bug:  If you did a VM_addr() on an object
 *			which was on disk, with the dirty flag set,then changed
 *			the contents, and then did a VM_addr() on a different
 *			object which caused the first object to be paged out
 *			before you did another VM_addr() on the first object
 *			with the dirty flag set - You would loose the changes
 *			you made to the first object.
 *
 *			atexit() is employed to free memory and delete the swap
 *			file on exit.
 *
 *			memcpy() replaced with memmove() to allow for 
 *			overlapping memory moves which does occur in compact().
 *
 *			Added a global variable (VM_newadd) which is set 
 *			to 1 whenever memory compaction or object swap out 
 *			occurs.  This variable should be reset to 0 by the 
 *			program using the VMEM system.  Since memory compaction
 *			and object swap out are the only two ways a pointer
 *			returned by VM_addr() can be invalidated (other than
 *			the application freeing the object) the application
 *			may do several VM_addr() on different VM handles 
 *			without freezing them in memory and 
 *			be assured of their continued validity until VM_newadd
 *			is set to 1 (at which point something was moved or
 *			swapped out and VM_addr() will be needed to obtain
 *			the new addresses.)  This variable was added so that
 *			an application may perform local buffering of the 
 *			addresses, and only needs to call the VMEM system
 *			when absolutely necessary.
 *
 *
 *		Version 3.3 - 5/12/89
 *
 *			Corrected bugs found by John McNamee:
 *
 *			Fixed bug in VM_alloc() where "h" was used before
 *			being assigned to.  The problem existed in the code
 *			that clears MT_IMEDIATE blocks.
 *
 *			Fixed bug in VM_addr() where, under curtain 
 *			circumstances, if a block is frozen and then unfrozen 
 *			the block would not be put back on the LRU chain.
 *			Causing it to never be paged out.
 *			
 *			Added VM_heapwalk() written by John McNamee.
 *
 *
 *		Version 3.4 - 7/20/90
 *
 *			Fixed argument order bug in rest_clean().
 *
 *		Version 3.5 - 9/30/90
 *
 *			Fixed bug in rmfree().
 *
 *		Version 3.6 - 3/30/91
 *
 *			Added support for Turbo C and UNIX.
 *
 *			Enhanced support for huge allocations.
 *
 *		Version 4.0 - 1/22/14
 *
 *			Updated system for ANSI C
*/

#include <stdlib.h>
#include <string.h>
#include "vmem.h"

/*  
    Currently setup for MicroSoft C and Turbo C and UNIX - they handle huge 
    pointers differently and have different huge allocation functions.
*/


#include <stdio.h>
#ifdef _MSC_VER
#include <dos.h>
#include <io.h>
#else
#define	O_BINARY	0
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


/*  #define DEBUG	*/

/* #define  USE_MALLOC */	/*  Force malloc instead of huge alloc
				   function */

#if !defined(__TURBOC__)  &&  !defined(_MSC_VER)
#define	USE_MALLOC
#endif

#ifndef OPEN
#define OPEN		open
#endif

#ifndef	DEBUG
#define	TEST(f,n)
#endif

#define FNAME(x)	static char fun[] = #x

#ifdef __TURBOC__
#define NO_ALLOCA 1024
#endif

#ifdef	unix
#define	NO_ALLOCA 4096
#define	NO_ATEXIT
#define	NO_MEMMOVE
#endif

#ifdef	_MSC_VER

#define	HPtoL(p)  (((unsigned long)FP_SEG(p)<<4) + (unsigned long)FP_OFF(p))
static	char HUGE   *HPtoFP();	/*  don't use this with OS/2 (use the #else)  */
#define	EQ(x,y)		(long)((char huge *) (x) - (char huge *) (y)) == 0L
#define	GT(x,y)		(long)((char huge *) (x) - (char huge *) (y)) >	 0L
#define	LT(x,y)		(long)((char huge *) (x) - (char huge *) (y)) <	 0L
#define	GTE(x,y)	(long)((char huge *) (x) - (char huge *) (y)) >= 0L

#else

#define	HPtoL(p)	p
#define	HPtoFP(p)	p
#define	EQ(x,y)		x == y
#define	GT(x,y)		x >  y
#define	LT(x,y)		x <  y
#define	GTE(x,y)	x >= y

#endif


#define	MT_NOTUSED	0x00	/*  free vm header			*/
#define	MT_IMEDIATE	0x01	/*  object located in vm header		*/
#define	MT_MEMORY	0x02	/*  object in real memory		*/
#define	MT_DISK		0x04	/*  object located on disk		*/
#define	MT_DIRTY	0x08	/*  object has been modified		*/
#define	MT_FREEZE	0x10	/*  object frozen in real memory	*/

#define	VMBASESIZ	255
#define	VMLEGSIZ	255
#define	MAXSALOC	500000L	/*  maximum single object size		*/
#define	RMCHUNK(x) ((x + sizeof(RMHEAD)	- 1) / sizeof(RMHEAD)) * sizeof(RMHEAD)
#define	DHEADSIZE	100	/* number of disk free pointers	in core	block */
#define	IMSIZE		(sizeof(VMbase[0]->mru_link) + sizeof(VMbase[0]->lru_link) + sizeof(VMbase[0]->size) + sizeof(VMbase[0]->mem) +	sizeof(VMbase[0]->diskadd))
#define FILE_BLOCK	16384	/* largest single file block  */


typedef	unsigned long	MAX_SIZ_TYP;

typedef	union  {
	struct	{
		LEG_TYPE	l;
		BASE_TYPE	b;
	}	p;
	VMPTR_TYPE	i;
}	VMPTR;

typedef	struct	VMSTRUCT  {
	char	type;
	VMPTR	mru_link;
	VMPTR	lru_link;
	MAX_SIZ_TYP	size;
	char	HUGE	*mem;
	long	diskadd;
}	VMHEAD;

typedef	int	ALIGN;

typedef	union	RMSTRUCT  {
	struct	{
		union	RMSTRUCT  HUGE	    *next;
		long	size;
	}	s;
	ALIGN	x;
}	RMHEAD;

typedef	RMHEAD HUGE *RMHEAD_PTR;

typedef	struct	DFSTRUCT  {
	long	diskaddr[DHEADSIZE];
	long	nfree[DHEADSIZE];
	struct	DFSTRUCT   HUGE	*next;
}	DFREE;


static	int	makemore(long s);
static	int	morecore(MAX_SIZ_TYP s);
static	void	rmfree(RMHEAD_PTR h, MAX_SIZ_TYP n);
static	void	vmfree_head(VMPTR_TYPE i);
static	void	vm_link(VMHEAD HUGE *h, VMPTR_TYPE i);
static	void	vm_unlink(VMHEAD HUGE *h);
static	void	compact(void);
static	void	d_compact(void);
static	void	d_compact1(void);
static	void	d_compact2(void);
static	void	dm_new(void);
static	void	page_out(VMHEAD	HUGE *h);
static	long	disk_next(long s, char HUGE *buf);
static	VMHEAD	HUGE	*page_in(VMHEAD	HUGE *h, VMPTR_TYPE i);
static	int	free_disk(MAX_SIZ_TYP sz, long da, int naf);
static	int	make_swap(void);
static	DFREE HUGE *dfree_new(void);
static	void	dfree_free(void);
static	long	get_la(long la);
static	VMHEAD HUGE  *get_dmem(long da);
static	void	rest_clean(int lev, int h, int mb, int mi);
static	void	error(char *f, char *m);

static	long	longwrite(), longread();
static	char	HUGE	*rmalloc();

extern	long	lseek();

static	RMHEAD_PTR  RMfree = NULL;		/* pointer to free real	memory chain  */
static	RMHEAD_PTR  RMsmem = NULL;		/* keep	track of system	allocations for	future release	*/
static	long	RMtotal	 = 0L;			/* total allocated from	system	*/
static	long	RMnfree	 = 0L;			/* free	bytes		*/
static	long	RMmax	 = 0L;			/* max allocated from system	*/
static	long	RMasize	 = RMCHUNK(8000);	/* allocation chunk size	*/
static	double	RMcompf	 = 2.0;			/* compaction factor	*/
static	int	RMncomp	 = 0;			/* number of compactions	*/

static	VMHEAD	HUGE	*VMbase[VMBASESIZ];	/* vm header table	*/
static	VMPTR	VMfree;				/* head	of free	vm header chain	*/
static	VMPTR	VMmru;				/* pointer to head of mru chain	*/
static	VMPTR	VMlru;				/* pointer to tail of mru chain	*/
static	VMPTR_TYPE VMlive = 0;			/* number of arrays in real memory	*/
static	VMPTR_TYPE VMdisk = 0;			/* number of arrays in disk memory	*/
static	long	VMtotal	= 0L;			/* total allocated virtual space	*/
static	VMPTR_TYPE	VMnfreez = 0;		/* total frozen	objects			*/
static	VMPTR_TYPE	VMnifrez = 0;		/* number of MT_IMEDIATEs frozen	*/

static	long	DMnext	 = 0L;		/* number of bytes used	on disk	*/
static	long	DMnfree	 = 0L;		/* number of free bytes	in swap	file */
static	long	DMmfree	 = 0L;		/* max free space on swap file before compaction	*/
static	int	DMneedflg= 0;		/* 1=couldn't find space on swap file */
static	int	DMctype	 = 0;		/* compaction type		*/
static	int	DMncomp	 = 0;		/* number of disk compactions	*/
static	int	DMnfblks = 0;		/* number of free blocks in swap file	*/
static	int	DMmfblks = 0;		/* number of free blocks in swap file before disk compaction	    */
static	int	DMhandle = -1;		/* swap	file handle		*/
static	DFREE	HUGE *DMflist = NULL;	/* list	of free	areas		*/
static	char	DMfile[50];		/* swap	file name		*/

int	VM_newadd = 0;			/* set to 1 when memory changed	*/


#ifdef	NO_MEMMOVE
static char	*memmove();
#endif

long	*VM_stat(void)
{
	static	long	s[16];

	s[ 0] =	RMtotal;
	s[ 1] =	RMnfree;
	s[ 2] =	RMmax;
	s[ 3] =	RMasize;
	s[ 4] =	RMncomp;
	s[ 5] =	VMlive;
	s[ 6] =	VMdisk;
	s[ 7] =	VMtotal;
	s[ 8] =	VMnfreez;
	s[ 9] =	DMnext;
	s[10] =	DMnfree;
	s[11] =	DMmfree;
	s[12] =	DMctype;
	s[13] =	DMncomp;
	s[14] =	DMnfblks;
	s[15] =	DMmfblks;
	return(s);
}

void	VM_parm(long rmmax, long rmasize, double rmcompf, long dmmfree, int dmmfblks, int dmctype)
{
	if (rmmax    !=	-1L)	
		RMmax	 = rmmax;
	if (rmasize  !=	-1L)	
		RMasize	 = RMCHUNK(rmasize);
	if (RMmax  &&  RMmax < RMasize)
		RMasize = RMmax;
	if (rmcompf  >=	0.0)	
		RMcompf	 = rmcompf;
	if (dmmfree  !=	-1L)	
		DMmfree	 = dmmfree;
	if (dmmfblks !=	-1 )	
		DMmfblks = dmmfblks;
	if (dmctype  !=	-1 )	
		DMctype	 = dmctype;
}

static	char	HUGE	*rmalloc(MAX_SIZ_TYP n)
{
	RMHEAD_PTR	h = NULL, b = NULL, p, pb;
	long	s, d = 0L;
	int	f=1;
	FNAME(rmalloc);

	s = RMCHUNK(n);
	while (!h  &&  !b)  {
		for (p=NULL, h = RMfree	; h ; p=h, h = h->s.next)  {
			if (h->s.size == s)	break;
			if (h->s.size >	s  &&  (!d  ||	h->s.size - s <	d))  {
				d = h->s.size -	s;
				pb = p;
				b = h;
			}
		}
		if (!h	&&  !b)
			if (f  &&  RMcompf  &&	RMnfree	> s * RMcompf)	{
				f = 0;
				compact();
			}  else	if (morecore((MAX_SIZ_TYP) s))	{
				if (makemore(s))
					return NULL;
				else	f = 1;
			}  else	   f = 0;
	}
	RMnfree	-= s;
	if (h)	{
		if (p == NULL)
			RMfree = h->s.next;
		else
			p->s.next = h->s.next;
		return HPtoFP((char HUGE *) h);
	}
	h = s /	(long) sizeof(RMHEAD) +	b;
	h->s.size = d;
	h->s.next = b->s.next;
	if (pb == NULL)
		RMfree = h;
	else
		pb->s.next = h;
	return HPtoFP((char HUGE *) b);
}

static	int	makemore(long s)
{
	VMHEAD	HUGE	*h;

	if (DMhandle ==	-1  ||	!VMlru.i)     return(1);
	while (s > 0L  &&  VMlru.i)  {
		h = &VMbase[VMlru.p.b][VMlru.p.l];
		page_out(h);
		vm_unlink(h);
		s -= h->size;
	}
	return(0);
}

static	int	morecore(MAX_SIZ_TYP s)
{
	RMHEAD_PTR	p;
	unsigned long	n;
#if defined(__TURBOC__)  &&  !defined(USE_MALLOC)
	char HUGE *farmalloc();
#endif
#if defined(_MSC_VER)  &&  !defined(USE_MALLOC)
	char HUGE *halloc();
#endif
	FNAME(morecore);

	s += RMCHUNK(sizeof(RMHEAD));	/* allow room for system pointer	*/
#if 0
	n = s >	RMasize	? s : RMasize;
	n = RMCHUNK(n);
#else
	n = (((long) s - 1L) / RMasize + 1L) * RMasize;
#endif
	if (RMmax  &&  RMtotal + n > RMmax)
		return(1);
#if defined(__TURBOC__)  &&  !defined(USE_MALLOC)
	p = (RMHEAD_PTR) farmalloc(n);
#else
#if defined(_MSC_VER)  &&  !defined(USE_MALLOC)
	p = (RMHEAD_PTR) halloc(n, 1);
#else
	p = (RMHEAD_PTR) malloc((unsigned) n);
#endif
#endif
	if (!p)
		return(1);
	RMtotal	 += n;
	p->s.next = RMsmem;
	RMsmem	  = p;
	p->s.size = n;
	rmfree((RMHEAD_PTR) (p+1), (MAX_SIZ_TYP) (n-sizeof(RMHEAD)));
	return(0);
}

void	VM_fcore(void)
{
	RMHEAD_PTR	n, p;

	VM_end();
	for (p=RMsmem ;	p ; p=n)  {
		n = p->s.next;
#if defined(__TURBOC__)  &&  !defined(USE_MALLOC)
		farfree((char HUGE *) p);
#else
#if defined(_MSC_VER)  &&  !defined(USE_MALLOC)
		hfree((char HUGE *) p);
#else
		free((char *) p);
#endif
#endif
	}
	RMsmem	= NULL;
	RMnfree	= 0L;
	RMfree	= NULL;
	RMtotal	= 0L;
	RMncomp	= 0;
	TEST("VM_fcore", 1);
}

static	void	rmfree(RMHEAD_PTR h, MAX_SIZ_TYP n)
{
	RMHEAD_PTR	f;
#ifdef	_MSC_VER
	RMHEAD_PTR	t;
#endif

	n = RMCHUNK(n);
	RMnfree	 += n;
	h->s.size = n;
	if (!RMfree)  {
		RMfree = h;
		h->s.next = NULL;
		return;
	}
	if (GT(RMfree, h))  {
		if (EQ(h + n / sizeof(RMHEAD), RMfree))	 {
			h->s.size += RMfree->s.size;
			h->s.next =  RMfree->s.next;
			RMfree = h;
			return;
		}
		h->s.next = RMfree;
		RMfree = h;
		return;
	}
	for (f=RMfree ;	f->s.next ; f=f->s.next)
		if (LT(f, h)  &&  LT(h, f->s.next))
			break;
#ifdef	_MSC_VER	    /*	for some reason	MSC-4.0	won't compile without t  */
	t = f +	(f->s.size / (long) sizeof(RMHEAD));
	if (EQ(t, h))
#else
	if (EQ(f + f->s.size / (long) sizeof(RMHEAD), h))
#endif
		f->s.size += h->s.size;
	else  {
		h->s.next = f->s.next;
		f->s.next = h;
		f = h;
	}
	if (f->s.next  &&  EQ(f	+ f->s.size / (long) sizeof(RMHEAD), f->s.next))  {
		f->s.size += f->s.next->s.size;
		f->s.next =  f->s.next->s.next;
	}
}

static	VMPTR_TYPE	vmget_head(void)
{
	VMPTR_TYPE	r;
	register int	b, i;
	VMHEAD	HUGE	*h;
	FNAME(vmget_head);

	if (!VMfree.i)	{
		for (b=0 ;  b != VMBASESIZ  &&	VMbase[b]  ;  ++b);
		if (b == VMBASESIZ)	return((VMPTR_TYPE) 0);
		h = VMbase[b] =	(VMHEAD	HUGE *) rmalloc((MAX_SIZ_TYP)(VMLEGSIZ *	sizeof(VMHEAD)));
		if (!VMbase[b])		return((VMPTR_TYPE) 0);
		if (b)
			i = 0;
		else  {		/* 0,0 is not used (its	NULL)	*/
			i = 1;
			h[0].type	= MT_NOTUSED;
			h[0].lru_link.i	= 0;
		}
		VMfree.p.b = b;
		VMfree.p.l = i;
		while (i != (VMLEGSIZ-1))  {
			h[i].type = MT_NOTUSED;
			h[i].lru_link.p.b = b;
			h[i].lru_link.p.l = 1 +	i;
			h[i++].diskadd = -1L;
		}
		h[i].type = MT_NOTUSED;
		h[i].lru_link.i	= 0;
		h[i].diskadd = -1L;
	}
	r = VMfree.i;
	VMfree.i = VMbase[VMfree.p.b][VMfree.p.l].lru_link.i;
	return(r);
}

static	void	vmfree_head(VMPTR_TYPE i)
{
	VMPTR	t;

	t.i = i;
	VMbase[t.p.b][t.p.l].type	= MT_NOTUSED;
	VMbase[t.p.b][t.p.l].lru_link.i	= VMfree.i;
	VMbase[t.p.b][t.p.l].diskadd	= -1L;
	VMfree.i = i;
}

VMPTR_TYPE	VM_alloc(long s, int zero)
{
	VMHEAD	HUGE	*h;
	VMPTR	p;
	register char	 HUGE	 *t;
	FNAME(VM_alloc);

	TEST(fun, 0);
#if (defined(__TURBOC__)  ||  defined(_MSC_VER))  &&  defined(USE_MALLOC)
	if (s > 65534U)
		return(0);
#endif
	if (s >	MAXSALOC  ||  s < 1L)			
		return(0);
	if (DMhandle ==	-1  &&	VM_init())	
		return(0);
	d_compact();
	TEST(fun, 1);
	if (!(p.i = vmget_head()))		
		return(0);
	if (s <= IMSIZE)  {
		h = &VMbase[p.p.b][p.p.l];
		h->type = MT_IMEDIATE;
		VMlive++;

		if (zero)
			memset((char HUGE *) &h->mru_link, 0, IMSIZE);

		TEST(fun, 2);
		return(p.i);
	}

/* &VMbase[x][y] cannot	be done	before rmalloc() bacause rmalloc may change the	address	of VMbase[x] during compact()	*/

	t = rmalloc((MAX_SIZ_TYP) s);
	h = &VMbase[p.p.b][p.p.l];
	h->mem = t;
	if (!h->mem)  {
		vmfree_head(p.i);
		TEST(fun, 3);
		return(0);
	}
	h->type	= MT_MEMORY;
	vm_link(h, p.i);
	h->size	   = s;
	h->diskadd = -1L;
	VMtotal	  += s;
	VMlive++;
	TEST(fun, 4);

	if (zero)  {
		for (t=h->mem ; s-- ; )
			*t++ = '\0';
		h->type	|= MT_DIRTY;
	}

	return(p.i);
}

char	HUGE	*VM_addr(VMPTR_TYPE i, int dirty, int frez)
{
	VMPTR	p;
	VMHEAD	HUGE *h, HUGE *t;
	FNAME(VM_addr);

	TEST(fun, 0);
	p.i = i;
	if (!i	||  p.p.b >= VMBASESIZ	||  p.p.l >= VMLEGSIZ  ||  !VMbase[p.p.b])
		return NULL;
	h = (VMHEAD HUGE	*) &VMbase[p.p.b][p.p.l];
	if (h->type == MT_NOTUSED)
		return NULL;
	if (h->type & MT_IMEDIATE)  {
		if (!frez  !=  !(h->type & MT_FREEZE))
			if (frez) {
				VMnfreez++;
				VMnifrez++;
				h->type	|= MT_FREEZE;
			}  else	 {
				VMnfreez--;
				VMnifrez--;
				h->type	&= (~MT_FREEZE);
			}
		TEST(fun, 1);
		return (char HUGE *) &h->mru_link;
	}
	if (h->type & MT_MEMORY)  {
		if (!(h->type &	MT_FREEZE)  &&	(h->mru_link.i || frez))
			vm_unlink(h);
		if (!frez  &&  (h->mru_link.i  ||  h->type & MT_FREEZE))
			vm_link(h, i);
	}  else	 {	/*  MT_DISK	*/
		d_compact();

  /*  d_compact	may call d_compact1, dfree_new,	rmalloc, compact and change the	address	 */

		h = (VMHEAD HUGE *) &VMbase[p.p.b][p.p.l];

		h = page_in(h, p.i);
		if (!h)
			return(NULL);
		if (!frez)
			vm_link(h, i);
	}
	if (dirty)
		h->type	|= MT_DIRTY;

	if (!frez  !=  !(h->type & MT_FREEZE))
		if (frez)  {
			VMnfreez++;
			h->type	|= MT_FREEZE;
		}  else	 {
			VMnfreez--;
			h->type	&= (~MT_FREEZE);
		}

	TEST(fun, 2);
	return (char HUGE *) h->mem;
}

static	void	vm_link(VMHEAD HUGE *h, VMPTR_TYPE i)	/*  add	to head	of lru link	*/
{
	h->mru_link.i =	0;
	if (h->lru_link.i = VMmru.i)
		VMbase[h->lru_link.p.b][h->lru_link.p.l].mru_link.i = i;
	VMmru.i	= i;
	if (!VMlru.i)
		VMlru.i	= i;
}

static	void	vm_unlink(VMHEAD HUGE *h)	/*  take out of	lru link	*/
{
	if (h->mru_link.i)
		VMbase[h->mru_link.p.b][h->mru_link.p.l].lru_link.i = h->lru_link.i;
	else
		VMmru.i	= h->lru_link.i;
	if (h->lru_link.i)
		VMbase[h->lru_link.p.b][h->lru_link.p.l].mru_link.i = h->mru_link.i;
	else
		VMlru.i	= h->mru_link.i;
}

VMPTR_TYPE	VM_realloc(VMPTR_TYPE i, long s)
{
	VMPTR_TYPE	n;
	char	HUGE	*f, HUGE	*t;
	VMPTR	p;
	VMHEAD	HUGE	*h;

	f = VM_addr(i, 0, 0);
	if (!f)
		return(0);
	p.i = i;
	h = (VMHEAD HUGE	*) &VMbase[p.p.b][p.p.l];
	if (h->type & MT_IMEDIATE  &&  s <= IMSIZE)
		return(i);
	if (h->type & MT_MEMORY	 &&  s == h->size)
		return(i);
	n = VM_alloc(s, 0);
	if (!n)
		return(0);
	t = VM_addr(n, 1, 0);

	/* just	in case VM_alloc did a compact	*/

	h = (VMHEAD HUGE *) &VMbase[p.p.b][p.p.l];

	/* in case both arrays can't be in memory  */

	if (h->type & MT_DISK)	{
		VM_free(n);
		return(0);
	}
	f = VM_addr(i, 0, 0);
	if (h->type & MT_IMEDIATE  ||  s <= IMSIZE)
		s = IMSIZE;
	else
		s = s <	h->size	? s : h->size;
	memcpy(t, f, (unsigned)	s);
	VM_free(i);
	return(n);
}

void	VM_free(VMPTR_TYPE i)
{
	VMPTR	p;
	VMHEAD	HUGE *h, HUGE *t;
	FNAME(VM_free);

	TEST(fun, 0);
	p.i = i;
	if (!i	||  p.p.b >= VMBASESIZ	||  p.p.l >= VMLEGSIZ  ||  !VMbase[p.p.b])
		return;
	h = (VMHEAD HUGE *) &VMbase[p.p.b][p.p.l];
	if (h->type == MT_NOTUSED)
		return;
	if (h->type & MT_IMEDIATE)  {
		if (h->type & MT_FREEZE)  {
			VMnfreez--;
			VMnifrez--;
		}
		vmfree_head(i);
		VMlive--;
		TEST(fun, 1);
		return;
	}
	VMtotal	-= h->size;
	free_disk(h->size, h->diskadd, 0);

	/* this	must be	redone because free_disk calls dfree_new, rmalloc, compact so the address may change  */

	h = (VMHEAD HUGE	*) &VMbase[p.p.b][p.p.l];

	if (h->type & MT_FREEZE)
		VMnfreez--;
	if (h->type & MT_DISK) {
		vmfree_head(i);
		VMdisk--;
		TEST(fun, 2);
		return;
	}
	if (!(h->type &	MT_FREEZE))
		vm_unlink(h);
	rmfree((RMHEAD_PTR) h->mem, h->size);
	vmfree_head(i);
	VMlive--;
	TEST(fun, 3);
}

static	void	compact(void)
{
	RMHEAD_PTR	h, a, p=NULL, t;
	register int	b, i;
	int	f, fm;
	VMHEAD	HUGE *v;
	RMHEAD	s;
	DFREE	HUGE	*ca, HUGE *pa;

	VM_newadd = 1;
	RMncomp++;
	for (h=RMfree ;	h ; )  {
		a = h +	(h->s.size / (long) sizeof(RMHEAD));
		f = fm = 1;
		s.s.next = h->s.next;
		s.s.size = h->s.size;
		for (b=0 ; b !=	VMBASESIZ  &&  f  &&  fm  &&  (v=VMbase[b]) ; ++b)
			if (EQ(a, (RMHEAD_PTR) v))
				if (!VMnifrez)	{

			 /* make sure this is the far memmove()	!!!   */

					memmove((char HUGE *) h, (char HUGE *) v, (unsigned) (sizeof(VMHEAD) * VMLEGSIZ));
					VMbase[b] = (VMHEAD HUGE *) HPtoFP((char	HUGE *)	h);
					h += ((VMLEGSIZ	* sizeof(VMHEAD)) + sizeof(RMHEAD) - 1)	/ sizeof(RMHEAD);
					f = 0;
				}  else
					fm = 0;

			else for (i=b?0:1 ; i != VMLEGSIZ  &&  f  &&  fm ; ++i)
				if (v[i].type &	MT_MEMORY  &&  EQ((RMHEAD_PTR) v[i].mem, a))
					if (!(v[i].type	& MT_FREEZE))  {

			 /* make sure this is the far memmove()	!!!   */

						memmove((char HUGE *) h, v[i].mem, (unsigned) v[i].size);
						v[i].mem = HPtoFP((char	HUGE *)	h);
						h += (v[i].size	+ sizeof(RMHEAD) - 1) /	sizeof(RMHEAD);
						f = 0;
					}  else
						fm = 0;

		if (f  &&  fm)
			for (pa=NULL,ca=DMflist	; ca ; pa=ca, ca=ca->next)
				if (EQ(a, (RMHEAD_PTR) ca))  {
					memmove((char HUGE *) h, (char *) ca, (unsigned) sizeof(DFREE));
					if (pa)
						pa->next = (DFREE HUGE *) HPtoFP((char HUGE *) h);
					else
						DMflist	 = (DFREE HUGE *) HPtoFP((char HUGE *) h);
					h += ((sizeof(DFREE) + sizeof(RMHEAD) -	1) / sizeof(RMHEAD));
					f = 0;
					break;
				}

		if (!f)	 {
			h->s.next = s.s.next;
			h->s.size = s.s.size;
			if (p == NULL)
				RMfree = h;
			else
				p->s.next = h;
			if (h->s.next  &&  EQ(h	+ h->s.size / (long) sizeof(RMHEAD), h->s.next))  {
				h->s.size += h->s.next->s.size;
				h->s.next =  h->s.next->s.next;
			}
		}  else	 {
			p = h;
			h = h->s.next;
		}
	}
}

int	VM_init(void)
{
	int	r;
	static	int	once = 1;

	if (DMhandle !=	-1)
		return(0);
	if (once)  {
		once = 0;
#ifndef	NO_ATEXIT
		atexit(VM_fcore);
#endif
	}
	dm_new();
	VMlive	 = 0;
	VMdisk	 = 0;
	VMnfreez = 0;
	VMnifrez = 0;
	DMncomp	 = 0;
	if (r=make_swap())
		return(r);
	return(0);
}

static	void	dm_new(void)
{
	DMneedflg    = 0;
	DMnext	     = 0L;
	DMnfree	     = 0L;
	DMnfblks     = 0;
	DMhandle     = -1;
	dfree_free();
}

static	void	page_out(VMHEAD	HUGE *h)
{
	FNAME(page_out);

	if (!(h->type &	MT_MEMORY))
		error(fun, "not MT_MEMORY");
	if (h->type & MT_FREEZE)
		error(fun, "can't page out frozen memory");
	if (h->type & MT_DIRTY)	 {
		if (h->diskadd == -1L)
			h->diskadd = disk_next((long) h->size, h->mem);
		else  {
			if (-1L	== lseek(DMhandle, h->diskadd, SEEK_SET))
				error(fun, "lseek");
			if (h->size != longwrite(DMhandle, h->mem, (long) h->size))
				error(fun, "write");
		}
	}
	rmfree((RMHEAD_PTR) h->mem, h->size);
	h->type	= MT_DISK;
	VMlive--;
	VMdisk++;
	VM_newadd = 1;
}

static	long	disk_next(long s, char HUGE *buf)
{
	/* variable prefix:  b=best,  c=current	 */
	/* variable suffix:  a=address,	s=size,	i=index	 */

	register int	ci, bi;
	long	bs, cs,	da = -1L;
	DFREE HUGE *ca, HUGE *ba;
	int	rtf = 1;
	FNAME(disk_next);

retry:
	if (DMnfree >= s) {
		bi = -1;
		bs = 0L;
		ca = DMflist;
		for ( ;	ca  &&	bs != s	; ca = ca->next)
			for (ci=0 ; ci != DHEADSIZE  ; ++ci)
				if ((cs=ca->nfree[ci]) == s)  {
					bi = ci;
					bs = cs;
					ba = ca;
					break;
				}  else	if (cs > s  &&	(cs < bs || bi == -1))	{
					bi = ci;
					bs = cs;
					ba = ca;
				}
		if (bs >= s)  {
			da = ba->diskaddr[bi];
			DMnfree	-= s;
			if (bs == s)  {
				DMnfblks--;
				ba->diskaddr[bi] = -1L;
			}  else
				ba->diskaddr[bi] += s;
			ba->nfree[bi] -= s;
		}
	}
	if (da == -1L)	{
		long	sw;

		if (-1L	== lseek(DMhandle, 0L, SEEK_END))
			error(fun, "lseek");
		if ((long) s != (sw = longwrite(DMhandle, buf, (long) s)))

/*  by checking	DMflist	I know that d_compact1 will not	rmalloc	which is something disk_next must NEVER	do */

			if (rtf	 &&  DMflist)  {
				long	ta = DMnext;
				int	add;

				if (sw != -1L)  {
					DMnext += sw;
					add = free_disk((MAX_SIZ_TYP) sw, ta, 1);
				}
				d_compact1();
				if (sw != -1L  &&  add)
					if (free_disk((MAX_SIZ_TYP) sw,	ta, 1))
						error(fun, "internal");
				rtf = 0;
				goto retry;
			}  else
				error(fun, "write");
		DMneedflg = 1;	/* I had to extend the swap file to meet system	needs  */
		da = DMnext;
		DMnext += s;
	}  else	 {
		DMneedflg = 0;	/* no need to extend swap file	*/
		if (-1L	== lseek(DMhandle, da, SEEK_SET))
			error(fun, "lseek");
		if ((long) s != longwrite(DMhandle, buf, (long) s))
			error(fun, "write");
	}
	return(da);
}

static	VMHEAD	HUGE	*page_in(VMHEAD	HUGE *h, VMPTR_TYPE i)
{
	VMPTR	p;
	char HUGE  *t;
	FNAME(page_in);

	p.i = i;
	if (!(h->type &	MT_DISK))		error(fun, "not MT_DISK");
	if (DMhandle ==	-1)			error(fun, "no swap file");
	if (!(t	= rmalloc(h->size)))
		return NULL;

/*  this must be done because rmalloc calls compact() which may	change the address of h	 */

	h = (VMHEAD HUGE *) &VMbase[p.p.b][p.p.l];
	h->mem = t;
	if (h->diskadd != -1L)	{
		if (-1L	== lseek(DMhandle, h->diskadd, SEEK_SET))
			error(fun, "lseek");
		if ((long) h->size != longread(DMhandle, h->mem, (long) h->size))
			error(fun, "read");
	}
	h->type	= MT_MEMORY;
	VMdisk--;
	VMlive++;
	return(h);
}

static	int	free_disk(MAX_SIZ_TYP sz, long da, int naf)
/* int		naf;	  no allocations flag		*/
{
	register int	ci;	/*  current index into disk free block	*/
	int	hflg = 1;	/*  0 indicates	head hit		*/
	int	tflg = 1;	/*  0 indicates	tail hit		*/
	DFREE  HUGE *ca = DMflist; /*  address of current free block	  */
	long	ma = da	+ sz;	/* tail	address	for match */
	long	ss = sz;	/*  search size		*/
	DFREE	HUGE *ha;	/*  header free	block address	*/
	int	hi;		/*  header index		*/
	DFREE	HUGE *ta;	/*  tail free block address	*/
	int	ti;		/*  tail index			*/
	DFREE	HUGE *za;	/*  zero free block address	*/
	int	zi = -1;	/*  zero index			*/
	FNAME(free_disk);

	if (-1 == DMhandle  ||	da == -1L)	return(1);
	for ( ;	ca  &&	(hflg  ||  tflg) ; ca =	ca->next)
		for (ci=0 ; ci != DHEADSIZE && (hflg ||	tflg) ;	++ci)
			if (ca->nfree[ci])  {
				if (hflg && ca->diskaddr[ci] + ca->nfree[ci] ==	da)  {
					hflg = 0;
					ha = ca;
					hi = ci;
					ca->nfree[ci] += ss;
				}  else	 if (tflg  && ma == ca->diskaddr[ci])  {
					long	siz = ca->nfree[ci];

					tflg = 0;
					ta = ca;
					ti = ci;
					ca->nfree[ci] =	0L;
					ca->diskaddr[ci] = -1L;
					if (!hflg)     /* header found */
						ha->nfree[hi] += siz;
					else
						ss += siz;
				}
			}  else	 if (zi	== -1)	{
				zi = ci;
				za = ca;
			}
	if (hflg)  {				/*  no header merged	*/
		if (tflg)  {			/*  no tail found	*/
			if (zi == -1)  {	/*  no empty space	*/
				if (naf)	return(1);
				za = dfree_new();
				za->nfree[0]	= ss;
				za->diskaddr[0]	= da;

			}  else	 {	/*  empty space	exists	*/

				za->nfree[zi]	 = ss;
				za->diskaddr[zi] = da;
			}
		}  else	 {	      /*  tail found - no header      */
			ta->nfree[ti]	+= ss;
			ta->diskaddr[ti] = da;
		}
	}

	DMnfree	  += sz;
	if (hflg  &&  tflg)
		DMnfblks++;
	else if	(!hflg	&&  !tflg)
		DMnfblks--;
	return(0);	/*  all	ok	*/
}

static	int	make_swap(void)
{
	char	*p, *mktemp(), *getenv();
	FNAME(make_swap);

	if (p =	getenv("VMPATH"))  {
		strcpy(DMfile, p);
		p = DMfile + strlen(DMfile) - 1;
		if (p >= DMfile	 &&  *p	!= '/'	&&  *p != '\\')
			strcat(DMfile, "/");
	}  else
		*DMfile	= '\0';
	strcat(DMfile, "vmXXXXXX");
	if (!mktemp(DMfile))
		return(1);
	DMhandle = OPEN(DMfile, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
	if (DMhandle ==	-1)
		return(2);
	return(0);
}

static	void	d_compact(void)
{
	if (DMneedflg  &&  (DMmfree  &&	 DMnfree > DMmfree  ||	DMmfblks  &&  DMnfblks > DMmfblks))
		VM_dcmps();
}

void	VM_dcmps(void)
{
	if (DMctype)
		d_compact2();	/* use 2 files	*/
	else
		d_compact1();	/* in place compaction	*/
	++DMncomp;
	DMneedflg = 0;
}


/*  this compactor requires two	files	*/

static	void	d_compact2(void)
{
	register int	 b, i;
	VMHEAD	HUGE	*v;
	int	handle;
	char	fn[sizeof DMfile];
#ifdef NO_ALLOCA
	char	dbuf[NO_ALLOCA];
	unsigned	bufsiz = NO_ALLOCA;
#else
	char	*dbuf;
	unsigned	bufsiz = 2048;
#endif
	FNAME(d_compact2);

#ifndef NO_ALLOCA
	while (NULL == (dbuf = alloca(bufsiz)))
		bufsiz /= 2;
#endif
	strcpy(fn, DMfile);
	handle = DMhandle;
	dm_new();
	if (make_swap())
		error(fun, "make_swap");
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)
			if ((v[i].type & (MT_MEMORY | MT_DISK))	 &&  v[i].diskadd != -1L)
				if (v[i].type &	MT_MEMORY)  {
					v[i].type |= MT_DIRTY;
					v[i].diskadd = -1L;
				}  else	 {
					unsigned int n = bufsiz	< v[i].size ? bufsiz : v[i].size;
					MAX_SIZ_TYP	m = v[i].size;

					if (-1L	== lseek(handle, v[i].diskadd, SEEK_SET))
						error(fun, "lseek");
					while (m)  {
						n = n >	m ? m :	n;
						if (n != read(handle, dbuf, n))
							error(fun, "read");
						if (n != write(DMhandle, dbuf, n))
							error(fun, "write");
						m -= n;
					}
					v[i].diskadd = DMnext;
					DMnext += v[i].size;
				}
	if (-1 == close(handle))
		error(fun, "close");
	if (-1 == unlink(fn))
		error(fun, "unlink");
}

static	void	d_compact1(void)
{
	register int	 b, i;
	VMHEAD	HUGE	*v;
#ifdef NO_ALLOCA
	char	dbuf[NO_ALLOCA];
	unsigned	bufsiz = NO_ALLOCA;
#else
	char	*dbuf;
	unsigned	bufsiz = 2048;
#endif
	long	la = -1L, get_la();
	unsigned int	n;
	MAX_SIZ_TYP	m;
	FNAME(d_compact1);

#ifndef NO_ALLOCA
	while (NULL == (dbuf = alloca(bufsiz)))
		bufsiz /= 2;
#endif
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)
			if (v[i].type &	MT_MEMORY  &&  v[i].diskadd != -1L)  {
				if (la == -1L  ||  v[i].diskadd	< la)
					la = v[i].diskadd;
				v[i].diskadd = -1L;
				v[i].type |= MT_DIRTY;
				DMnfree	+= v[i].size;
			}

	la = get_la(la);
	while (v = get_dmem(la))  {
		n = bufsiz < v->size ? bufsiz :	v->size;
		m = v->size;
		while (m)  {
			n = n >	m ? m :	n;
			if (-1L	== lseek(DMhandle, v->diskadd+v->size-m, SEEK_SET))
				error(fun, "lseek");
			if (n != read(DMhandle,	dbuf, n))
				error(fun, "read");
			if (-1L	== lseek(DMhandle, la+v->size-m, SEEK_SET))
				error(fun, "lseek");
			if (n != write(DMhandle, dbuf, n))
				error(fun, "write");
			m -= n;
		}
		v->diskadd = la;
		la += v->size;
	}
	dfree_free();
	dfree_new();
	DMflist->nfree[0]    = DMnfree;
	DMflist->diskaddr[0] = la;
	DMnfblks = 1;
}

static	DFREE HUGE *dfree_new(void)
{
	DFREE HUGE *d, HUGE *ca;
	register int	i;
	FNAME(dfree_new);

	d = (DFREE HUGE *) rmalloc((MAX_SIZ_TYP)	sizeof(DFREE));
	if (!d)
		error(fun, "rmalloc");
	for (i=0 ; i !=	DHEADSIZE ; ++i)  {
		d->nfree[i]    = 0L;
		d->diskaddr[i] = -1L;
	}
	d->next	= NULL;
	if (DMflist)  {
		for (ca=DMflist	; ca->next ; ca=ca->next);
		ca->next = d;
	}  else
		DMflist	= d;
	return(d);
}

static	void	dfree_free(void)
{
	DFREE	HUGE	*ca;

	while (DMflist)	 {
		ca = DMflist;
		DMflist	= DMflist->next;
		rmfree((RMHEAD_PTR) ca,	(MAX_SIZ_TYP) sizeof(DFREE));
	}
}

static	long	get_la(long la)    /*  get lowest free block	address	 */
{
	register int	ci;	/*  current index into disk free block	*/
	DFREE HUGE *ca =	DMflist; /*  address of	current	free block	 */
	FNAME(get_la);

	for ( ;	ca ; ca	= ca->next)
		for (ci=0 ; ci != DHEADSIZE ; ++ci)
			if (ca->nfree[ci]  &&  (la == -1L  ||  ca->diskaddr[ci]	< la))
				la = ca->diskaddr[ci];
	return(la);
}

static	VMHEAD HUGE  *get_dmem(long da)    /* get vm pointer which is stored just past da  */
{
	register int	 b, i;
	VMHEAD	HUGE	*v, HUGE *r = NULL;
	long	ba = DMnext;
	FNAME(get_dmem);

	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)
			if (v[i].type &	MT_DISK	 &&  v[i].diskadd > da	&&  v[i].diskadd < ba)	{
				ba = v[i].diskadd;
				r  = v + i;
			}
	return(r);
}

int	VM_dump(char *f)
{
	short	mb;
	register int	i, b;
	int	h, s = (sizeof(VMHEAD) * VMLEGSIZ);
#ifdef NO_ALLOCA
	char	dbuf[NO_ALLOCA];
	unsigned	bufsiz = NO_ALLOCA;
#else
	char	*dbuf;
	unsigned	bufsiz = 2048;
#endif
	VMHEAD	HUGE *v;
	FNAME(VM_dump);

	TEST(fun, 0);
	h = OPEN(f, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
	if (h == -1)		return(1);
#ifndef NO_ALLOCA
	while (NULL == (dbuf = alloca(bufsiz)))
		bufsiz /= 2;
#endif
	if (sizeof(VMfree) != write(h, (char *)	&VMfree, sizeof(VMfree)))
		goto er2;

	for (mb=0 ; mb != VMBASESIZ  &&	 VMbase[mb] ; ++mb);
	if (sizeof(mb) != write(h, (char *) &mb, sizeof(mb)))
		goto er2;
	for (i=0 ; i !=	mb ; ++i)
		if (s != write(h, (char	HUGE *) VMbase[i], s))
			goto er2;
	for (b=0 ; b !=	mb ; ++b)  {
		v = VMbase[b];
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)  {
			if (v[i].type &	MT_MEMORY)  {
				if ((long) v[i].size != longwrite(h, v[i].mem, (long) v[i].size))
					goto er2;
			}  else	 if (v[i].type & MT_DISK  &&  v[i].diskadd != -1L)  {
				unsigned int n = bufsiz	< v[i].size ? bufsiz : v[i].size;
				MAX_SIZ_TYP	m = v[i].size;

				if (-1L	== lseek(DMhandle, v[i].diskadd, SEEK_SET))
					error(fun, "lseek");
				while (m)  {
					n = n >	m ? m :	n;
					if (n != read(DMhandle,	dbuf, n))
						error(fun, "read");
					if (n != write(h, dbuf,	n))
						goto er2;
					m -= n;
				}
			}
		}
	}
	if (-1 == close(h))
		error(fun, "close");
	TEST(fun, 1);
	return(0);
er2:
	if (-1 == close(h))
		error(fun, "close");
	if (-1 == unlink(f))
		error(fun, "unlink");
	TEST(fun, 2);
	return(2);		/*  write error	*/
}

void	VM_end(void)
{
	register int	i, b;
	VMHEAD	HUGE	*v;
	FNAME(VM_end);

	TEST(fun, 0);
	if (DMhandle !=	-1)  {
		if (-1 == close(DMhandle))
			error(fun, "close");
		if (-1 == unlink(DMfile))
			error(fun, "unlink");
	}
	dm_new();
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)  {
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)
			if (v[i].type &	MT_MEMORY)
				rmfree((RMHEAD_PTR) v[i].mem, v[i].size);
		rmfree((RMHEAD_PTR) v, (MAX_SIZ_TYP) (sizeof(VMHEAD) * VMLEGSIZ));
		VMbase[b] = NULL;
	}
	VMfree.i = 0;
	VMmru.i	 = 0;
	VMlru.i	 = 0;
	VMtotal	 = 0L;
	VMlive	 = 0;
	VMdisk	 = 0;
	VMnfreez = 0;
	VMnifrez = 0;
	TEST(fun, 1);
}

int	VM_rest(char *f)
{
	short	mb;
	register int	i, b;
	VMPTR	p;
	int	h, s = (sizeof(VMHEAD) * VMLEGSIZ);
	VMHEAD	HUGE	*v;
	char	HUGE	*t;
	FNAME(VM_rest);

	TEST(fun, 0);
	VM_end();
	VM_init();
	h = OPEN(f, O_RDONLY | O_BINARY);
	if (h == -1)		return(1);
	if (sizeof(VMfree) != read(h, (char *) &VMfree,	sizeof(VMfree)))  {
		rest_clean(1, h, -1, -1);
		return(2);
	}
	if (sizeof(mb) != read(h, (char	*) &mb,	sizeof(mb)))  {
		rest_clean(1, h, -1, -1);
		return(2);
	}
	for (i=0 ; i !=	mb ; ++i)  {
		VMbase[i] = (VMHEAD HUGE *) rmalloc((MAX_SIZ_TYP) s);
		if (!VMbase[i])	 {
			rest_clean(2, h, -1, -1);
			return(3);
		}
		if (s != read(h, (char HUGE *) VMbase[i], s))  {
			rest_clean(2, h, -1, -1);
			return(2);
		}
	}
	for (b=0 ; b !=	mb ; ++b)  {
		v = VMbase[b];
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)  {
			if (v[i].type != MT_NOTUSED  &&	 !(v[i].type & MT_IMEDIATE))
				VMtotal	+= v[i].size;
			if (v[i].type != MT_NOTUSED  &&	 (!(v[i].type &	MT_DISK)  ||  v[i].diskadd != -1L))
				VMlive++;
			if (v[i].type &	MT_MEMORY  ||  v[i].type & MT_DISK  &&	v[i].diskadd !=	-1L)  {
				v[i].type = MT_MEMORY |	MT_DIRTY;
				if (!(t	= rmalloc(v[i].size)))	{
					rest_clean(3, h, b, i);
					return(3);
				}

/* this	must be	done because rmalloc calls compact which may change the	address	of VMbase[x]  */
				v = VMbase[b];
				v[i].mem = t;

				if ((long) v[i].size != longread(h, v[i].mem, (long) v[i].size))  {
					rest_clean(3, h, b, i+1);
					return(2);
				}
				p.p.b =	b;
				p.p.l =	i;
				vm_link(&v[i], p.i);
				v[i].diskadd = -1L;
			}
		}
	}
	if (-1 == close(h))
		error(fun, "close");
	TEST(fun, 1);
	return(0);
}

int	VM_frest(char *f)
{
	short	mb;
	register int	i, b;
	int	h, s = (sizeof(VMHEAD) * VMLEGSIZ);
	VMHEAD	HUGE	*v;
	long	da, tell();
	FNAME(VM_frest);

	TEST(fun, 0);
	VM_end();
	TEST(fun, 2);
	h = OPEN(f, O_RDWR | O_BINARY);
	if (h == -1)		return(1);
	if (sizeof(VMfree) != read(h, (char *) &VMfree,	sizeof(VMfree)))  {
		rest_clean(1, h, -1, -1);
		return(2);
	}
	if (sizeof(mb) != read(h, (char	*) &mb,	sizeof(mb)))  {
		rest_clean(1, h, -1, -1);
		return(2);
	}
	for (i=0 ; i !=	mb ; ++i)  {
		VMbase[i] = (VMHEAD HUGE *) rmalloc((MAX_SIZ_TYP) s);
		if (!VMbase[i])	 {
			rest_clean(2, h, -1, -1);
			return(3);
		}
		if (s != read(h, (char HUGE *) VMbase[i], s))  {
			rest_clean(2, h, -1, -1);
			return(2);
		}
	}
	da = tell(h);
	dfree_new();
	DMflist->diskaddr[0] = 0L;
	DMnfree	= DMflist->nfree[0] = da;
	DMnfblks = 1;
	for (b=0 ; b !=	mb ; ++b)  {
		v = VMbase[b];
		for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)  {
			if (v[i].type != MT_NOTUSED  &&	 !(v[i].type & MT_IMEDIATE))
				VMtotal	+= v[i].size;
			else if	(v[i].type & MT_IMEDIATE)
				VMlive++;
			if (v[i].type &	MT_MEMORY  ||  v[i].type & MT_DISK  &&	v[i].diskadd !=	-1L)  {
				v[i].type = MT_DISK;
				v[i].diskadd = da;
				da += v[i].size;
				VMdisk++;
			}
		}
	}
	DMhandle = h;
	strcpy(DMfile, f);
	DMnext = da;
	TEST(fun, 1);
	return(0);
}

static	void	rest_clean(int lev, int h, int mb, int mi)
{
	register int	b, i;
	VMHEAD HUGE	*v;

	close(h);
	VMfree.i = 0;
	if (lev	<= 1)	return;
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)  {
		if (lev	== 3  &&  b <= mb)
			for (i=b?0:1 ; i != VMLEGSIZ  ;	++i)
				if (v[i].type &	MT_MEMORY  &&  (b != mb	 ||  i < mi))
					rmfree((RMHEAD_PTR) v[i].mem, v[i].size);
		rmfree((RMHEAD_PTR) v, (MAX_SIZ_TYP) (sizeof(VMHEAD) * VMLEGSIZ));
		VMbase[b] = NULL;
	}
	if (lev	== 3)  {
		VMtotal	= 0L;
		VMlive	= 0;
	}
}

static	long	longread(int fh, char HUGE *buf, long n)
{
	int	len, len2;
	long	tot = 0L;
	
	while (n)  {
		len = FILE_BLOCK < n ? FILE_BLOCK : n;
		len2 = read(fh, buf, len);
		if (len2 < 0)
			return (long) len2;
		tot += len2;
		if (len2 != len)
			return tot;
		n -= len;
		buf += len;
	}
	return tot;
}

static	long	longwrite(int fh, char HUGE *buf, long n)
{
	int	len, len2;
	long	tot = 0L;
	
	while (n)  {
		len = FILE_BLOCK < n ? FILE_BLOCK : n;
		len2 = write(fh, buf, len);
		if (len2 < 0)
			return (long) len2;
		tot += len2;
		if (len2 != len)
			return tot;
		n -= len;
		buf += len;
	}
	return tot;
}

#ifdef	_MSC_VER

/*
    This function should not be	used in	OS/2 because you can't just use any
    valid memory selector (segment).
*/


static	char HUGE   *HPtoFP(char HUGE *n)	/* huge	pointer	to far pointer	*/
{
	register unsigned  s, o;
	unsigned	d;
	char	HUGE	*p;

	o = FP_OFF(n);
	s = FP_SEG(n);
	if (o >	16)  {
		d = o &	(~0xf);
		o -= d;
		s += (d>>4);
	}
	FP_OFF(p) = o;
	FP_SEG(p) = s;
	return(p);
}

#endif

static	void	error(char *f, char *m)
{
	fprintf(stderr,	"\nVM system error - %s, %s\n",	f, m);
	exit(-1);
}

void VM_heapwalk(FILE *fp)
{
	VMHEAD HUGE *v;
	VMPTR handle;
	register int b, i;

	fprintf(fp, "Virual Memory Heap Walk\n");
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b) {
		fprintf(fp, "\nVMbase[%u] = %p\n\n", b, v);
		for (i=0 ; i !=	VMLEGSIZ  ; ++i,++v) {
			handle.p.b = b;
			handle.p.l = i;
			fprintf(fp, "[%04X] %p - ", handle.i, v);
			switch (v->type	& ~(MT_DIRTY | MT_FREEZE)) {
				case MT_NOTUSED:
					fprintf(fp, "Free\n");
					break;
				case MT_IMEDIATE:
					fprintf(fp, "Immediate storage\n");
					break;
				case MT_MEMORY:
					fprintf(fp, "%lu bytes at %p", v->size, v->mem);
					if (v->diskadd != -1L)
						fprintf(fp, " (On disk at %08lX)", v->diskadd);
					if (v->type & MT_DIRTY)
						fprintf(fp, " (Dirty)");
					if (v->type & MT_FREEZE)
						fprintf(fp, " (Locked)");
					fprintf(fp, "\n");
					break;
				case MT_DISK:
					fprintf(fp, "%lu bytes on disk at %08lX\n", v->size, v->diskadd);
					break;
				default:
					fprintf(fp, "Invalid memory type\n");
					break;
			}
		}
	}
	fflush(fp);
}

#ifdef	DEBUG

static	TEST(char *fun, int n)
{
	long	trm, tdm;

	vm_test(fun, n,	&trm, &tdm);
	rm_test(fun, n,	trm);
	dm_test(fun, n,	tdm);
}

static	rm_test(char *fun, int n, long trm)
{
	RMHEAD_PTR	h;
	long	s;
	void	terror();

	s = 0L;
	for (h = RMfree	; h ; h	= h->s.next)  {
		if (h->s.size <	sizeof(RMHEAD))
			terror(fun, n, "Invalid RM size");
		s += h->s.size;
		if (h->s.next  &&  GTE(h, h->s.next))
			terror(fun, n, "RMfree not always increasing address");
	}
	if (RMnfree != s)
		terror(fun, n, "RMnfree != total of free list");
	if (RMtotal != trm + RMnfree)
		terror(fun, n, "RMtotal not all accessable");
}

static	dm_test(char *fun, int n, long tdm)
{
	register int	ci;
	long	ts, cs,	da;
	DFREE	HUGE  *ca;
	int	nfb = 0;

	if (DMhandle ==	-1)	return;
	ts = 0L;
	for (ca=DMflist	; ca ; ca = ca->next)
		for (ci=0 ; ci != DHEADSIZE  ; ++ci)  {
			da = ca->diskaddr[ci];
			cs = ca->nfree[ci];
			if (da != -1L)	{
				if (da < 0L  ||	 da >= DMnext)
					terror(fun, n, "bad disk address");
				if (cs <= 0L  ||  cs > DMnext -	ts)
					terror(fun, n, "bad size");
				ts += cs;
				nfb++;
			}  else	 if (cs)
				terror(fun, n, "no disk address, yes size");
		}
	if (ts != DMnfree)
		terror(fun, n, "bad DMnfree");
	if (-1L	== (da = lseek(DMhandle, 0L, SEEK_END)))
		terror(fun, n, "lseek");
	if (DMnext != da)
		terror(fun, n, "bad DMnext");
	if (tdm	+ DMnfree != DMnext)
		terror(fun, n, "Not all disk memory accessable");
	if (nfb	!= DMnfblks)
		terror(fun, n, "DMnfblks wrong");
}

static	vm_test(char *fun, int n, long *ptrm, long *ptdm)
{
	register int	b, i;
	int	mb;
	VMHEAD	HUGE *v;
	VMPTR	p, f;
	long	trm = 0L, tdm =	0L, trm2 = 0L, tlm = 0L, tvm = 0L, tfm = 0L, tsm = 0L, tfrm = 0L;
	VMPTR_TYPE	live = 0, disk = 0, nfreeze = 0, nifrez	= 0;
	DFREE	HUGE	*ca;
	RMHEAD_PTR	sm;

	for (mb=0 ; mb != VMBASESIZ  &&	 VMbase[mb] ; ++mb);
	for (b=0 ; b !=	VMBASESIZ  &&  (v=VMbase[b]) ; ++b)  {
		tlm += RMCHUNK((VMLEGSIZ * sizeof(VMHEAD)));
		for (i=0 ; i !=	VMLEGSIZ  ; ++i,++v)
			switch (v->type	& ~(MT_DIRTY | MT_FREEZE))  {
				case MT_NOTUSED:
					if (v->type)
						terror(fun, n, "Invalid memory type");
					if (v->lru_link.i)  {
						if (v->lru_link.p.b >= mb  ||  v->lru_link.p.l >= VMLEGSIZ)
							terror(fun, n, "Invalid lru link");
					}
					break;
				case MT_IMEDIATE:
					live++;
					if (v->type & MT_FREEZE)  {
						nfreeze++;
						nifrez++;
					}
					break;
				case MT_MEMORY:
					if (!(v->type &	MT_FREEZE))  {
						if (v->mru_link.i)  {
							if (v->mru_link.p.b >= mb  ||  v->mru_link.p.l >= VMLEGSIZ)
								terror(fun, n, "Invalid mru link");
						}  else
							if (VMmru.p.b != b  ||	VMmru.p.l != i)
								terror(fun, n, "Bad VMmru or mru link");
						if (v->lru_link.i)  {
							if (v->lru_link.p.b >= mb  ||  v->lru_link.p.l >= VMLEGSIZ)
								terror(fun, n, "Invalid lru link");
						}  else
							if (VMlru.p.b != b  ||	VMlru.p.l != i)
								terror(fun, n, "Bad VMlru or lru link");
					}
					if (v->size <= IMSIZE)
						terror(fun, n, "Invalid size");
					trm += RMCHUNK(v->size);
					if (v->type & MT_FREEZE)  {
						tfrm +=	RMCHUNK(v->size);
						nfreeze++;
					}
					live++;
					disk--;
					/* no break  */
				case MT_DISK:
					disk++;
					if (v->diskadd != -1L)	{
						if (v->diskadd < 0  ||	v->diskadd >= DMnext)
							terror(fun, n, "Invalid disk address");
						tdm += v->size;
					}
					tvm += v->size;
					break;
				default:
					terror(fun, n, "Invalid memory type");
					break;
			}
	}
	for (f.i=0,p.i = VMmru.i ; p.i	; p.i =	v->lru_link.i)	{
		if (p.p.b >= mb	 ||  p.p.l >= VMLEGSIZ)
			terror(fun, n, "Invalid pointer");
		v = &VMbase[p.p.b][p.p.l];
		if (!(v->type &	MT_MEMORY))
			terror(fun, n, "Invalid memory type");
		if (v->type & MT_FREEZE)
			terror(fun, n, "Frozen memory on lru link");
		trm2 +=	RMCHUNK(v->size);
		if (v->mru_link.i != f.i)
			terror(fun, n, "Invalid mru link");
		f.i = p.i;
	}
	for (ca=DMflist	; ca ; ca=ca->next)
		tfm += RMCHUNK(sizeof(DFREE));
	for (sm=RMsmem ; sm ; sm=sm->s.next)
		tsm += sizeof(RMHEAD);
	if (f.i	!= VMlru.i)
		terror(fun, n, "Invalid lru link");
	if (trm2 + tfrm	!= trm)
		terror(fun, n, "Missing memory");
	for (p.i = VMfree.i ; p.i  ; p.i = v->lru_link.i)  {
		if (p.p.b >= mb	 ||  p.p.l >= VMLEGSIZ)
			terror(fun, n, "Invalid free pointer");
		v = &VMbase[p.p.b][p.p.l];
		if (v->type)
			terror(fun, n, "Invalid type");
	}
	if (tvm	!= VMtotal)
		terror(fun, n, "VMtotal incorrect");
	if (live != VMlive)
		terror(fun, n, "VMlive incorrect");
	if (disk != VMdisk)
		terror(fun, n, "VMdisk incorrect");
	if (nfreeze != VMnfreez)
		terror(fun, n, "VMnfreez incorrect");
	if (nifrez != VMnifrez)
		terror(fun, n, "VMnifrez incorrect");
	*ptrm =	trm + tlm + tfm	+ tsm;
	*ptdm =	tdm;
}

static	void	terror(char *f, int n, char *m)
{
	fprintf(stderr,	"\nVM test error - %s, %d, %s\n", f, n,	m);
	exit(-1);
}


#endif

#ifdef	NO_MEMMOVE

static char	*memmove(char HUGE *t, char HUGE *f, unsigned n)
{
	char	*r = t;

	if (f == t || n	<= 0)
		return(r);
	if (f >	t)
		while (n--)
			*t++ = *f++;
	else  {
		t += n-1;
		f += n-1;
		while (n--)
			*t-- = *f--;
	}
	return(r);
}

#endif

