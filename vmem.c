/*
 *
 *	       Copyright (c) 1988  Blake McBride
 *	       All Rights Reserved.
 *
 *
 *	       Blake McBride
 *             blake@mcbridemail.com
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
 *
 *		Version 4.1 - 2/13/25
 *
 *			Modernized the code
 *
 *		Version 5.0 - 11/16/25
 *
 *			Significantly modernized the code
 *
 *			No longer assumes segmented memory
*/

#include <stdlib.h>
#include <string.h>
#include "vmem.h"

/*  
    Currently setup for MicroSoft C and Turbo C and UNIX - they handle huge 
    pointers differently and have different huge allocation functions.
*/


#include <stdio.h>
#include <errno.h>
#ifdef _MSC_VER
#include <dos.h>
#include <io.h>
#include <malloc.h>
#define mkstemp my_mkstemp
#define lseek _lseek
#else
#define	O_BINARY	0
#include <stdlib.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

/* Compatibility for file permissions */
#ifndef S_IREAD
#define S_IREAD S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE S_IWUSR
#endif


//#define DEBUG

#define  USE_MALLOC 	/*  Force malloc instead of huge alloc
				   function */

#ifndef OPEN
#define OPEN		open
#endif

#ifdef	DEBUG
static	void TEST(char *fun, int n);
static	void vm_test(char *fun, int n, long *ptrm, long *ptdm);
static	void rm_test(char *fun, int n, long trm);
static	void dm_test(char *fun, int n, long tdm);
static	void terror(char *f, int n, char *m);
#else
#define	TEST(f,n)
#endif

#ifdef DEBUG
#define FNAME(x)	static char fun[] = #x
#else
#define FNAME(x)	/* No-op when not debugging */
#endif

// Uncomment the following if you do not have alloca
//#define NO_ALLOCA 1024

#define	EQ(x,y)		x == y
#define	GT(x,y)		x >  y
#define	LT(x,y)		x <  y
#define	GTE(x,y)	x >= y


#define	MT_NOTUSED	0x00	/*  free vm header			*/
#define	MT_IMEDIATE	0x01	/*  object located in vm header		*/
#define	MT_MEMORY	0x02	/*  object in real memory		*/
#define	MT_DISK		0x04	/*  object located on disk		*/
#define	MT_DIRTY	0x08	/*  object has been modified		*/
#define	MT_FREEZE	0x10	/*  object frozen in real memory	*/

/* Linear array configuration */
#ifdef __LP64__
  #define	VMINITSIZE	1024     /* Initial allocation of VM headers */
  #define	VMGROWSIZE	1024     /* Growth increment when more headers needed */
  #define	VMMAXSIZE	1048576  /* Maximum number of VM objects (1M) */
#else
  #define	VMINITSIZE	256      /* Initial allocation for 16-bit systems */
  #define	VMGROWSIZE	256      /* Growth increment */
  #define	VMMAXSIZE	65535    /* Maximum for 16-bit index */
#endif

#define	MAXSALOC	500000L	/*  maximum single object size		*/
#define	RMCHUNK(x) ((x + sizeof(RMHEAD)	- 1) / sizeof(RMHEAD)) * sizeof(RMHEAD)
#define	DHEADSIZE	100	/* number of disk free pointers	in core	block */
#define	IMSIZE		(sizeof(VMbase[0].mru_link) + sizeof(VMbase[0].lru_link) + sizeof(VMbase[0].size) + sizeof(VMbase[0].mem) +	sizeof(VMbase[0].diskadd))
#define FILE_BLOCK	16384	/* largest single file block  */


typedef	unsigned long	MAX_SIZ_TYP;

typedef	struct	VMSTRUCT  {
	char	type;
	VMPTR_TYPE	mru_link;
	VMPTR_TYPE	lru_link;
	MAX_SIZ_TYP	size;
	char		*mem;
	long	diskadd;
}	VMHEAD;

typedef	int	ALIGN;

typedef	union	RMSTRUCT  {
	struct	{
		union	RMSTRUCT  	    *next;
		long	size;
	}	s;
	ALIGN	x;
}	RMHEAD;

typedef	RMHEAD  *RMHEAD_PTR;

typedef	struct	DFSTRUCT  {
	long	diskaddr[DHEADSIZE];
	long	nfree[DHEADSIZE];
	struct	DFSTRUCT   	*next;
}	DFREE;


static	int	makemore(long s);
static	int	morecore(MAX_SIZ_TYP s);
static	void	rmfree(RMHEAD_PTR h, MAX_SIZ_TYP n);
static	void	vmfree_head(VMPTR_TYPE i);
static	void	vm_link(VMHEAD  *h, VMPTR_TYPE i);
static	void	vm_unlink(VMHEAD  *h);
static	void	compact(void);
static	void	d_compact(void);
static	void	d_compact1(void);
static	void	d_compact2(void);
static	void	dm_new(void);
static	void	page_out(VMHEAD	 *h);
static	long	disk_next(long s, char  *buf);
static	VMHEAD		*page_in(VMHEAD	 *h, VMPTR_TYPE i);
static	int	free_disk(MAX_SIZ_TYP sz, long da, int naf);
static	int	make_swap(void);
static	DFREE  *dfree_new(void);
static	void	dfree_free(void);
static	long	get_la(long la);
static	VMHEAD   *get_dmem(long da);
static	void	error(const char *f, const char *m);

static	long	longwrite(int fh, char *buf, long n);
static	long	longread(int fh, char *buf, long n);
static	char	*rmalloc(MAX_SIZ_TYP n);

static	RMHEAD_PTR  RMfree = NULL;		/* pointer to free real	memory chain  */
static	RMHEAD_PTR  RMsmem = NULL;		/* keep	track of system	allocations for	future release	*/
static	long	RMtotal	 = 0L;			/* total allocated from	system	*/
static	long	RMnfree	 = 0L;			/* free	bytes		*/
static	long	RMmax	 = 0L;			/* max allocated from system	*/
static	long	RMasize	 = RMCHUNK(8000);	/* allocation chunk size	*/
static	double	RMcompf	 = 2.0;			/* compaction factor	*/
static	int	RMncomp	 = 0;               /* number of compactions	*/

static	VMHEAD	*VMbase = NULL;		/* vm header array (linear) */
static	VMPTR_TYPE VMallocated = 0;	/* number of allocated VM headers */
static	VMPTR_TYPE VMfree = 0;		/* head	of free	vm header chain	*/
static	VMPTR_TYPE VMmru = 0;		/* pointer to head of mru chain	*/
static	VMPTR_TYPE VMlru = 0;		/* pointer to tail of mru chain	*/
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
static	DFREE	 *DMflist = NULL;	/* list	of free	areas		*/
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

static	char		*rmalloc(MAX_SIZ_TYP n)
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
		if (!h	&&  !b) {
			if (f  &&  RMcompf  &&	RMnfree	> s * RMcompf)	{
				f = 0;
				compact();
			}  else	if (morecore((MAX_SIZ_TYP) s))	{
				if (makemore(s))
					return NULL;
				else	f = 1;
			}  else	   f = 0;
		}
	}
	RMnfree	-= s;
	if (h)	{
		if (p == NULL)
			RMfree = h->s.next;
		else
			p->s.next = h->s.next;
		return (char  *) h;
	}
	h = s /	(long) sizeof(RMHEAD) +	b;
	h->s.size = d;
	h->s.next = b->s.next;
	if (pb == NULL)
		RMfree = h;
	else
		pb->s.next = h;
	return (char  *) b;
}

static	int	makemore(long s)
{
	VMHEAD		*h;

	if (DMhandle ==	-1  ||	!VMlru)
		return(1);
	while (s > 0L  &&  VMlru)  {
		h = &VMbase[VMlru];
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
	p = (RMHEAD_PTR) malloc((unsigned) n);
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
		free((char *) p);
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
	if (EQ(f + f->s.size / (long) sizeof(RMHEAD), h))
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
	VMPTR_TYPE	i, new_size;
	VMHEAD		*new_base;
	FNAME(vmget_head);

	/* If no free headers, need to allocate or grow the array */
	if (!VMfree)	{
		/* Check if we need initial allocation */
		if (!VMbase) {
			VMallocated = VMINITSIZE;
			VMbase = (VMHEAD *) rmalloc((MAX_SIZ_TYP)(VMallocated * sizeof(VMHEAD)));
			if (!VMbase) {
				VMallocated = 0;
				return((VMPTR_TYPE) 0);
			}
			/* Initialize the free list, skip index 0 (NULL) */
			VMfree = 1;
			for (i = 1; i < VMallocated - 1; i++) {
				VMbase[i].type = MT_NOTUSED;
				VMbase[i].lru_link = i + 1;
				VMbase[i].diskadd = -1L;
			}
			VMbase[i].type = MT_NOTUSED;
			VMbase[i].lru_link = 0;
			VMbase[i].diskadd = -1L;
			/* Mark index 0 as permanently not used */
			VMbase[0].type = MT_NOTUSED;
			VMbase[0].lru_link = 0;
		}
		/* Grow the array if needed */
		else if (VMallocated < VMMAXSIZE) {
			new_size = VMallocated + VMGROWSIZE;
			if (new_size > VMMAXSIZE)
				new_size = VMMAXSIZE;

			/* Note: rmalloc may trigger compact() which can change VMbase */
			new_base = (VMHEAD *) rmalloc((MAX_SIZ_TYP)(new_size * sizeof(VMHEAD)));
			if (!new_base)
				return((VMPTR_TYPE) 0);

			/* Copy existing data */
			memcpy(new_base, VMbase, VMallocated * sizeof(VMHEAD));
			rmfree((RMHEAD_PTR)VMbase, (MAX_SIZ_TYP)(VMallocated * sizeof(VMHEAD)));
			VMbase = new_base;

			/* Initialize new entries and add to free list */
			VMfree = VMallocated;
			for (i = VMallocated; i < new_size - 1; i++) {
				VMbase[i].type = MT_NOTUSED;
				VMbase[i].lru_link = i + 1;
				VMbase[i].diskadd = -1L;
			}
			VMbase[i].type = MT_NOTUSED;
			VMbase[i].lru_link = 0;
			VMbase[i].diskadd = -1L;

			VMallocated = new_size;
		}
		else {
			/* Reached maximum capacity */
			return((VMPTR_TYPE) 0);
		}
	}

	/* Get the next free header */
	r = VMfree;
	VMfree = VMbase[VMfree].lru_link;
	return(r);
}

static	void	vmfree_head(VMPTR_TYPE i)
{
	VMbase[i].type = MT_NOTUSED;
	VMbase[i].lru_link = VMfree;
	VMbase[i].diskadd = -1L;
	VMfree = i;
}

VMPTR_TYPE	VM_alloc(long s, int zero)
{
	VMHEAD		*h;
	VMPTR_TYPE	p;
	register char	*t;
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
	if (!(p = vmget_head()))
		return(0);
	if (s <= IMSIZE)  {
		h = &VMbase[p];
		h->type = MT_IMEDIATE;
		VMlive++;

		if (zero)
			memset((char  *) &h->mru_link, 0, IMSIZE);

		TEST(fun, 2);
		return(p);
	}

/* VMbase can change during rmalloc() due to compact(), so get address after */

	t = rmalloc((MAX_SIZ_TYP) s);
	h = &VMbase[p];
	h->mem = t;
	if (!h->mem)  {
		vmfree_head(p);
		TEST(fun, 3);
		return(0);
	}
	h->type	= MT_MEMORY;
	vm_link(h, p);
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

	return(p);
}

void	*VM_addr(VMPTR_TYPE i, int dirty, int frez)
{
	VMHEAD	 *h;
	FNAME(VM_addr);

	TEST(fun, 0);
	if (!i	||  i >= VMallocated)
		return NULL;
	h = &VMbase[i];
	if (h->type == MT_NOTUSED)
		return NULL;
	if (h->type & MT_IMEDIATE)  {
		if (!frez  !=  !(h->type & MT_FREEZE)) {
			if (frez) {
				VMnfreez++;
				VMnifrez++;
				h->type	|= MT_FREEZE;
			}  else	 {
				VMnfreez--;
				VMnifrez--;
				h->type	&= (~MT_FREEZE);
			}
		}
		TEST(fun, 1);
		return (char  *) &h->mru_link;
	}
	if (h->type & MT_MEMORY)  {
		if (!(h->type &	MT_FREEZE)  &&	(h->mru_link || frez))
			vm_unlink(h);
		if (!frez  &&  (h->mru_link  ||  h->type & MT_FREEZE))
			vm_link(h, i);
	}  else	 {	/*  MT_DISK	*/
		d_compact();

  /*  d_compact	may call d_compact1, dfree_new,	rmalloc, compact and change the	address	 */

		h = &VMbase[i];

		h = page_in(h, i);
		if (!h)
			return(NULL);
		if (!frez)
			vm_link(h, i);
	}
	if (dirty)
		h->type	|= MT_DIRTY;

	if (!frez  !=  !(h->type & MT_FREEZE)) {
		if (frez)  {
			VMnfreez++;
			h->type	|= MT_FREEZE;
		}  else	 {
			VMnfreez--;
			h->type	&= (~MT_FREEZE);
		}
	}

	TEST(fun, 2);
	return (char  *) h->mem;
}

static	void	vm_link(VMHEAD  *h, VMPTR_TYPE i)	/*  add	to head	of lru link	*/
{
	h->mru_link = 0;
	if ((h->lru_link = VMmru))
		VMbase[h->lru_link].mru_link = i;
	VMmru = i;
	if (!VMlru)
		VMlru = i;
}

static	void	vm_unlink(VMHEAD  *h)	/*  take out of	lru link	*/
{
	if (h->mru_link)
		VMbase[h->mru_link].lru_link = h->lru_link;
	else
		VMmru = h->lru_link;
	if (h->lru_link)
		VMbase[h->lru_link].mru_link = h->mru_link;
	else
		VMlru = h->mru_link;
}

VMPTR_TYPE	VM_realloc(VMPTR_TYPE i, long s)
{
	VMPTR_TYPE	n;
	char  *f, *t;
	VMHEAD	*h;

	f = VM_addr(i, 0, 0);
	if (!f)
		return(0);
	h = &VMbase[i];
	if (h->type & MT_IMEDIATE  &&  s <= IMSIZE)
		return(i);
	if (h->type & MT_MEMORY	 &&  s == h->size)
		return(i);
	n = VM_alloc(s, 0);
	if (!n)
		return(0);
	t = VM_addr(n, 1, 0);

	/* just	in case VM_alloc did a compact	*/

	h = &VMbase[i];

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
	VMHEAD	 *h;
	FNAME(VM_free);

	TEST(fun, 0);
	if (!i	||  i >= VMallocated)
		return;
	h = &VMbase[i];
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

	h = &VMbase[i];

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
	RMHEAD_PTR	h, a, p=NULL;
	register int	i;
	int	f, fm;
	RMHEAD	s;
	DFREE	*ca,  *pa;

	VM_newadd = 1;
	RMncomp++;
	for (h=RMfree ;	h ; )  {
		a = h +	(h->s.size / (long) sizeof(RMHEAD));
		f = fm = 1;
		s.s.next = h->s.next;
		s.s.size = h->s.size;
		/* Check if VMbase array itself needs to be moved */
		if (VMbase && EQ(a, (RMHEAD_PTR) VMbase)) {
			if (!VMnifrez) {
				/* Move the entire VMbase array */
				memmove((char *) h, (char *) VMbase, (unsigned) (sizeof(VMHEAD) * VMallocated));
				VMbase = (VMHEAD *) h;
				h += ((VMallocated * sizeof(VMHEAD)) + sizeof(RMHEAD) - 1) / sizeof(RMHEAD);
				f = 0;
			} else
				fm = 0;
		}
		/* Check each VM object's memory */
		else if (VMbase) {
			for (i = 1; i < VMallocated && f && fm; ++i) {
				if (VMbase[i].type & MT_MEMORY && EQ((RMHEAD_PTR) VMbase[i].mem, a)) {
					if (!(VMbase[i].type & MT_FREEZE)) {
						/* Move the memory block */
						memmove((char *) h, VMbase[i].mem, (unsigned) VMbase[i].size);
						VMbase[i].mem = (char *) h;
						h += (VMbase[i].size + sizeof(RMHEAD) - 1) / sizeof(RMHEAD);
						f = 0;
					} else
						fm = 0;
				}
			}
		}

		if (f  &&  fm)
			for (pa=NULL,ca=DMflist	; ca ; pa=ca, ca=ca->next)
				if (EQ(a, (RMHEAD_PTR) ca))  {
					memmove((char  *) h, (char *) ca, (unsigned) sizeof(DFREE));
					if (pa)
						pa->next = (DFREE *) (char *) h;
					else
						DMflist	 = (DFREE *) (char  *) h;
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
	if ((r=make_swap()) != 0)
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

static	void	page_out(VMHEAD	 *h)
{
	FNAME(page_out);

	if (!(h->type &	MT_MEMORY))
		error(__func__, "not MT_MEMORY");
	if (h->type & MT_FREEZE)
		error(__func__, "can't page out frozen memory");
	if (h->type & MT_DIRTY)	 {
		if (h->diskadd == -1L)
			h->diskadd = disk_next((long) h->size, h->mem);
		else  {
			if (-1L	== lseek(DMhandle, h->diskadd, SEEK_SET))
				error(__func__, "lseek");
			if (h->size != longwrite(DMhandle, h->mem, (long) h->size))
				error(__func__, "write");
		}
	}
	rmfree((RMHEAD_PTR) h->mem, h->size);
	h->type	= MT_DISK;
	VMlive--;
	VMdisk++;
	VM_newadd = 1;
}

static	long	disk_next(long s, char  *buf)
{
	/* variable prefix:  b=best,  c=current	 */
	/* variable suffix:  a=address,	s=size,	i=index	 */

	register int	ci, bi;
	long	bs, cs,	da = -1L;
	DFREE  *ca,  *ba = NULL;
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
			error(__func__, "lseek");
		if ((long) s != (sw = longwrite(DMhandle, buf, (long) s))) {

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
						error(__func__, "internal");
				rtf = 0;
				goto retry;
			}  else
				error(__func__, "write");
		}
		DMneedflg = 1;	/* I had to extend the swap file to meet system	needs  */
		da = DMnext;
		DMnext += s;
	}  else	 {
		DMneedflg = 0;	/* no need to extend swap file	*/
		if (-1L	== lseek(DMhandle, da, SEEK_SET))
			error(__func__, "lseek");
		if ((long) s != longwrite(DMhandle, buf, (long) s))
			error(__func__, "write");
	}
	return(da);
}

static	VMHEAD		*page_in(VMHEAD	 *h, VMPTR_TYPE i)
{
	char   *t;
	FNAME(page_in);

	if (!(h->type &	MT_DISK))
		error(__func__, "not MT_DISK");
	if (DMhandle ==	-1)
		error(__func__, "no swap file");
	if (!(t	= rmalloc(h->size)))
		return NULL;

/*  this must be done because rmalloc calls compact() which may	change the address of h	 */

	h = &VMbase[i];
	h->mem = t;
	if (h->diskadd != -1L)	{
		if (-1L	== lseek(DMhandle, h->diskadd, SEEK_SET))
			error(__func__, "lseek");
		if ((long) h->size != longread(DMhandle, h->mem, (long) h->size))
			error(__func__, "read");
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
	DFREE   *ca = DMflist; /*  address of current free block	  */
	long	ma = da	+ sz;	/* tail	address	for match */
	long	ss = sz;	/*  search size		*/
	DFREE	 *ha;	/*  header free	block address	*/
	int	hi;		/*  header index		*/
	DFREE	 *ta = NULL;	/*  tail free block address	*/
	int	ti = -1;		/*  tail index			*/
	DFREE	 *za;	/*  zero free block address	*/
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
				if (naf)
					return(1);
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

	DMnfree	+= sz;
	if (hflg  &&  tflg)
		DMnfblks++;
	else if	(!hflg	&&  !tflg)
		DMnfblks--;
	return(0);	/*  all	ok	*/
}

static	int	make_swap(void)
{
	char	*p;
	FNAME(make_swap);

	if ((p = getenv("VMPATH")) != NULL)  {
		strcpy(DMfile, p);
		p = DMfile + strlen(DMfile) - 1;
		if (p >= DMfile	 &&  *p	!= '/'	&&  *p != '\\')
			strcat(DMfile, "/");
	}  else
		*DMfile	= '\0';
	strcat(DMfile, "vmXXXXXX");
	DMhandle = mkstemp(DMfile);
	if (DMhandle ==	-1)
		return(2);
#ifndef _MSC_VER
	ftruncate(DMhandle, 0);
	fchmod(DMhandle, S_IREAD | S_IWRITE);
#endif
	return(0);
}

static	void	d_compact(void)
{
	if (DMneedflg  &&  ((DMmfree  &&  DMnfree > DMmfree)  ||  (DMmfblks  &&  DMnfblks > DMmfblks)))
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
	register int	 i;
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
		error(__func__, "make_swap");
	if (VMbase) {
		for (i = 1 ; i < VMallocated ; ++i)
			if ((VMbase[i].type & (MT_MEMORY | MT_DISK)) && VMbase[i].diskadd != -1L) {
				if (VMbase[i].type &	MT_MEMORY)  {
					VMbase[i].type |= MT_DIRTY;
					VMbase[i].diskadd = -1L;
				}  else	 {
					unsigned int n = bufsiz	< VMbase[i].size ? bufsiz : VMbase[i].size;
					MAX_SIZ_TYP	m = VMbase[i].size;

					if (-1L	== lseek(handle, VMbase[i].diskadd, SEEK_SET))
						error(__func__, "lseek");
					while (m)  {
						n = n >	m ? m :	n;
						if (n != read(handle, dbuf, n))
							error(__func__, "read");
						if (n != write(DMhandle, dbuf, n))
							error(__func__, "write");
						m -= n;
					}
					VMbase[i].diskadd = DMnext;
					DMnext += VMbase[i].size;
				}
			}
	}
	if (-1 == close(handle))
		error(__func__, "close");
	if (-1 == unlink(fn))
		error(__func__, "unlink");
}

static	void	d_compact1(void)
{
	register int	 i;
	VMHEAD		*v;
#ifdef NO_ALLOCA
	char	dbuf[NO_ALLOCA];
	unsigned	bufsiz = NO_ALLOCA;
#else
	char	*dbuf;
	unsigned	bufsiz = 2048;
#endif
	long	la = -1L;
	unsigned int	n;
	MAX_SIZ_TYP	m;
	FNAME(d_compact1);

#ifndef NO_ALLOCA
	while (NULL == (dbuf = alloca(bufsiz)))
		bufsiz /= 2;
#endif
	if (VMbase) {
		for (i = 1 ; i < VMallocated ; ++i)
			if (VMbase[i].type &	MT_MEMORY  &&  VMbase[i].diskadd != -1L)  {
				if (la == -1L  ||  VMbase[i].diskadd	< la)
					la = VMbase[i].diskadd;
				VMbase[i].diskadd = -1L;
				VMbase[i].type |= MT_DIRTY;
				DMnfree	+= VMbase[i].size;
			}
	}

	la = get_la(la);
	while ((v = get_dmem(la)))  {
		n = bufsiz < v->size ? bufsiz :	v->size;
		m = v->size;
		while (m)  {
			n = n >	m ? m :	n;
			if (-1L	== lseek(DMhandle, v->diskadd+v->size-m, SEEK_SET))
				error(__func__, "lseek");
			if (n != read(DMhandle,	dbuf, n))
				error(__func__, "read");
			if (-1L	== lseek(DMhandle, la+v->size-m, SEEK_SET))
				error(__func__, "lseek");
			if (n != write(DMhandle, dbuf, n))
				error(__func__, "write");
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

static	DFREE  *dfree_new(void)
{
	DFREE  *d,  *ca;
	register int	i;
	FNAME(dfree_new);

	d = (DFREE  *) rmalloc((MAX_SIZ_TYP)	sizeof(DFREE));
	if (!d)
		error(__func__, "rmalloc");
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
	DFREE		*ca;

	while (DMflist)	 {
		ca = DMflist;
		DMflist	= DMflist->next;
		rmfree((RMHEAD_PTR) ca,	(MAX_SIZ_TYP) sizeof(DFREE));
	}
}

static	long	get_la(long la)    /*  get lowest free block	address	 */
{
	register int	ci;	/*  current index into disk free block	*/
	DFREE  *ca = DMflist; /*  address of	current	free block	 */
	FNAME(get_la);

	for ( ;	ca ; ca	= ca->next)
		for (ci=0 ; ci != DHEADSIZE ; ++ci)
			if (ca->nfree[ci]  &&  (la == -1L  ||  ca->diskaddr[ci]	< la))
				la = ca->diskaddr[ci];
	return(la);
}

static	VMHEAD   *get_dmem(long da)    /* get vm pointer which is stored just past da  */
{
	register int	 i;
	VMHEAD		*r = NULL;
	long	ba = DMnext;
	FNAME(get_dmem);

	if (VMbase) {
		for (i = 1 ; i < VMallocated ; ++i)
			if (VMbase[i].type &	MT_DISK	 &&  VMbase[i].diskadd > da	&&  VMbase[i].diskadd < ba)	{
				ba = VMbase[i].diskadd;
				r  = &VMbase[i];
			}
	}
	return(r);
}

/* Dump/restore for linear array implementation */

/* Dump file format:
 * 1. Magic number and version (8 bytes)
 * 2. VMallocated (sizeof(VMPTR_TYPE))
 * 3. VM global state (VMfree, VMmru, VMlru, etc.)
 * 4. VMbase array (VMallocated * sizeof(VMHEAD))
 * 5. Memory contents for each MT_MEMORY object
 */

#define DUMP_MAGIC	0x564D454D4C494E  /* "VMEMLIN" in hex */
#define DUMP_VERSION	1

int	VM_dump(char *f)
{
	int h;
	VMPTR_TYPE i;
	long magic = DUMP_MAGIC;
	int version = DUMP_VERSION;
	FNAME(VM_dump);

	TEST(fun, 0);

	/* Create dump file */
	h = open(f, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, S_IREAD | S_IWRITE);
	if (h == -1)
		return 1;

	/* Write magic number and version */
	if (write(h, &magic, sizeof(magic)) != sizeof(magic))
		goto error;
	if (write(h, &version, sizeof(version)) != sizeof(version))
		goto error;

	/* Write VM state variables */
	if (write(h, &VMallocated, sizeof(VMallocated)) != sizeof(VMallocated))
		goto error;
	if (write(h, &VMfree, sizeof(VMfree)) != sizeof(VMfree))
		goto error;
	if (write(h, &VMmru, sizeof(VMmru)) != sizeof(VMmru))
		goto error;
	if (write(h, &VMlru, sizeof(VMlru)) != sizeof(VMlru))
		goto error;
	if (write(h, &VMlive, sizeof(VMlive)) != sizeof(VMlive))
		goto error;
	if (write(h, &VMdisk, sizeof(VMdisk)) != sizeof(VMdisk))
		goto error;
	if (write(h, &VMtotal, sizeof(VMtotal)) != sizeof(VMtotal))
		goto error;
	if (write(h, &VMnfreez, sizeof(VMnfreez)) != sizeof(VMnfreez))
		goto error;
	if (write(h, &VMnifrez, sizeof(VMnifrez)) != sizeof(VMnifrez))
		goto error;

	/* Write VMbase array if it exists */
	if (VMbase && VMallocated > 0) {
		if (write(h, VMbase, VMallocated * sizeof(VMHEAD)) !=
		    VMallocated * sizeof(VMHEAD))
			goto error;

		/* Write memory contents for MT_MEMORY objects */
		for (i = 1; i < VMallocated; i++) {
			if (VMbase[i].type & MT_MEMORY) {
				if ((long)VMbase[i].size != longwrite(h, VMbase[i].mem, (long)VMbase[i].size))
					goto error;
			}
		}
	}

	close(h);
	TEST(fun, 1);
	return 0;

error:
	close(h);
	unlink(f);
	return 2;
}

int	VM_rest(char *f)
{
	int h;
	VMPTR_TYPE i, old_allocated;
	long magic;
	int version;
	char *mem;
	FNAME(VM_rest);

	TEST(fun, 0);

	/* Clean up current state */
	VM_end();
	if (VM_init())
		return 3;

	/* Open dump file */
	h = open(f, O_RDONLY | O_BINARY);
	if (h == -1)
		return 1;

	/* Read and verify magic number and version */
	if (read(h, &magic, sizeof(magic)) != sizeof(magic))
		goto error;
	if (magic != DUMP_MAGIC)
		goto error;
	if (read(h, &version, sizeof(version)) != sizeof(version))
		goto error;
	if (version != DUMP_VERSION)
		goto error;

	/* Read VM state variables */
	if (read(h, &old_allocated, sizeof(old_allocated)) != sizeof(old_allocated))
		goto error;
	if (read(h, &VMfree, sizeof(VMfree)) != sizeof(VMfree))
		goto error;
	if (read(h, &VMmru, sizeof(VMmru)) != sizeof(VMmru))
		goto error;
	if (read(h, &VMlru, sizeof(VMlru)) != sizeof(VMlru))
		goto error;
	if (read(h, &VMlive, sizeof(VMlive)) != sizeof(VMlive))
		goto error;
	if (read(h, &VMdisk, sizeof(VMdisk)) != sizeof(VMdisk))
		goto error;
	if (read(h, &VMtotal, sizeof(VMtotal)) != sizeof(VMtotal))
		goto error;
	if (read(h, &VMnfreez, sizeof(VMnfreez)) != sizeof(VMnfreez))
		goto error;
	if (read(h, &VMnifrez, sizeof(VMnifrez)) != sizeof(VMnifrez))
		goto error;

	/* Allocate VMbase array */
	if (old_allocated > 0) {
		VMbase = (VMHEAD *) rmalloc((MAX_SIZ_TYP)(old_allocated * sizeof(VMHEAD)));
		if (!VMbase)
			goto error;
		VMallocated = old_allocated;

		/* Read VMbase array */
		if (read(h, VMbase, VMallocated * sizeof(VMHEAD)) !=
		    VMallocated * sizeof(VMHEAD))
			goto error;

		/* Restore memory contents for MT_MEMORY objects */
		for (i = 1; i < VMallocated; i++) {
			if (VMbase[i].type & MT_MEMORY) {
				mem = rmalloc((MAX_SIZ_TYP)VMbase[i].size);
				if (!mem)
					goto error;
				VMbase[i].mem = mem;
				if ((long)VMbase[i].size != longread(h, VMbase[i].mem, (long)VMbase[i].size))
					goto error;
			}
		}
	}

	close(h);
	TEST(fun, 1);
	return 0;

error:
	close(h);
	VM_end();
	return 2;
}

int	VM_frest(char *f)
{
	int h;
	VMPTR_TYPE i, old_allocated;
	long magic, da = 0L;
	int version;
	FNAME(VM_frest);

	TEST(fun, 0);

	/* For fast restore, we keep the dump file as the swap file */
	VM_end();

	/* Open dump file */
	h = open(f, O_RDWR | O_BINARY);
	if (h == -1)
		return 1;

	/* Read and verify magic number and version */
	if (read(h, &magic, sizeof(magic)) != sizeof(magic)) {
		close(h);
		return 2;
	}
	if (magic != DUMP_MAGIC) {
		close(h);
		return 2;
	}
	if (read(h, &version, sizeof(version)) != sizeof(version)) {
		close(h);
		return 2;
	}
	if (version != DUMP_VERSION) {
		close(h);
		return 2;
	}

	/* Read VM state variables */
	if (read(h, &old_allocated, sizeof(old_allocated)) != sizeof(old_allocated) ||
	    read(h, &VMfree, sizeof(VMfree)) != sizeof(VMfree) ||
	    read(h, &VMmru, sizeof(VMmru)) != sizeof(VMmru) ||
	    read(h, &VMlru, sizeof(VMlru)) != sizeof(VMlru) ||
	    read(h, &VMlive, sizeof(VMlive)) != sizeof(VMlive) ||
	    read(h, &VMdisk, sizeof(VMdisk)) != sizeof(VMdisk) ||
	    read(h, &VMtotal, sizeof(VMtotal)) != sizeof(VMtotal) ||
	    read(h, &VMnfreez, sizeof(VMnfreez)) != sizeof(VMnfreez) ||
	    read(h, &VMnifrez, sizeof(VMnifrez)) != sizeof(VMnifrez)) {
		close(h);
		return 2;
	}

	/* Allocate VMbase array */
	if (old_allocated > 0) {
		VMbase = (VMHEAD *) rmalloc((MAX_SIZ_TYP)(old_allocated * sizeof(VMHEAD)));
		if (!VMbase) {
			close(h);
			return 2;
		}
		VMallocated = old_allocated;

		/* Read VMbase array */
		if (read(h, VMbase, VMallocated * sizeof(VMHEAD)) !=
		    VMallocated * sizeof(VMHEAD)) {
			close(h);
			VM_end();
			return 2;
		}

		/* Calculate disk addresses for fast restore */
		da = sizeof(magic) + sizeof(version) + sizeof(old_allocated) +
		     sizeof(VMfree) + sizeof(VMmru) + sizeof(VMlru) +
		     sizeof(VMlive) + sizeof(VMdisk) + sizeof(VMtotal) +
		     sizeof(VMnfreez) + sizeof(VMnifrez) +
		     VMallocated * sizeof(VMHEAD);

		/* Update disk addresses for objects stored in the file */
		for (i = 1; i < VMallocated; i++) {
			if (VMbase[i].type & MT_MEMORY) {
				/* These were saved in the dump, treat as on disk now */
				VMbase[i].type = MT_DISK;
				VMbase[i].diskadd = da;
				da += VMbase[i].size;
				VMdisk++;
				VMlive--;
			}
		}
	}

	/* Use dump file as swap file */
	DMhandle = h;
	strcpy(DMfile, f);
	DMnext = da;

	TEST(fun, 1);
	return 0;
}

void	VM_end(void)
{
	register int i;
	FNAME(VM_end);

	TEST(fun, 0);
	if (DMhandle != -1) {
		close(DMhandle);
		unlink(DMfile);
	}
	dm_new();

	if (VMbase) {
		/* Free all allocated memory blocks */
		for (i = 1; i < VMallocated; ++i) {
			if (VMbase[i].type & MT_MEMORY)
				rmfree((RMHEAD_PTR) VMbase[i].mem, VMbase[i].size);
		}
		/* Free the VMbase array itself */
		rmfree((RMHEAD_PTR) VMbase, (MAX_SIZ_TYP) (sizeof(VMHEAD) * VMallocated));
		VMbase = NULL;
	}

	VMallocated = 0;
	VMfree = 0;
	VMmru = 0;
	VMlru = 0;
	VMtotal = 0L;
	VMlive = 0;
	VMdisk = 0;
	VMnfreez = 0;
	VMnifrez = 0;
	TEST(fun, 1);
}

static	long	longread(int fh, char  *buf, long n)
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

static	long	longwrite(int fh, char  *buf, long n)
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

static	void	error(const char *f, const char *m)
{
	fprintf(stderr,	"\nVM system error - %s, %s\n",	f, m);
	exit(-1);
}

void VM_heapwalk(FILE *fp)
{
	VMHEAD  *v;
	VMPTR_TYPE handle;
	register int i;

	fprintf(fp, "Virtual Memory Heap Walk\n");
	if (VMbase) {
		fprintf(fp, "\nVMbase = %p, allocated = %u\n\n", VMbase, VMallocated);
		v = VMbase;
		for (i=0 ; i < VMallocated  ; ++i, ++v) {
			handle = i;
			fprintf(fp, "[%04X] %p - ", handle, v);
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

static	void TEST(char *fun, int n)
{
	long	trm, tdm;

	vm_test(fun, n,	&trm, &tdm);
	rm_test(fun, n,	trm);
	dm_test(fun, n,	tdm);
}

static	void rm_test(char *fun, int n, long trm)
{
	RMHEAD_PTR	h;
	long	s;

	s = 0L;
	for (h = RMfree	; h ; h	= h->s.next)  {
		if (h->s.size <	sizeof(RMHEAD))
			terror(__func__, n, "Invalid RM size");
		s += h->s.size;
		if (h->s.next  &&  GTE(h, h->s.next))
			terror(__func__, n, "RMfree not always increasing address");
	}
	if (RMnfree != s)
		terror(__func__, n, "RMnfree != total of free list");
	if (RMtotal != trm + RMnfree)
		terror(__func__, n, "RMtotal not all accessable");
}

static	void dm_test(char *fun, int n, long tdm)
{
	register int	ci;
	long	ts, cs,	da;
	DFREE	  *ca;
	int	nfb = 0;

	if (DMhandle ==	-1)	return;
	ts = 0L;
	for (ca=DMflist	; ca ; ca = ca->next)
		for (ci=0 ; ci != DHEADSIZE  ; ++ci)  {
			da = ca->diskaddr[ci];
			cs = ca->nfree[ci];
			if (da != -1L)	{
				if (da < 0L  ||	 da >= DMnext)
					terror(__func__, n, "bad disk address");
				if (cs <= 0L  ||  cs > DMnext -	ts)
					terror(__func__, n, "bad size");
				ts += cs;
				nfb++;
			}  else	 if (cs)
				terror(__func__, n, "no disk address, yes size");
		}
	if (ts != DMnfree)
		terror(__func__, n, "bad DMnfree");
	if (-1L	== (da = lseek(DMhandle, 0L, SEEK_END)))
		terror(__func__, n, "lseek");
	if (DMnext != da)
		terror(__func__, n, "bad DMnext");
	if (tdm	+ DMnfree != DMnext)
		terror(__func__, n, "Not all disk memory accessable");
	if (nfb	!= DMnfblks)
		terror(__func__, n, "DMnfblks wrong");
}

static	void vm_test(char *fun, int n, long *ptrm, long *ptdm)
{
	register int	b, i;
	int	mb;
	VMHEAD	 *v;
	VMPTR_TYPE	p, f;
	long	trm = 0L, tdm =	0L, trm2 = 0L, tlm = 0L, tvm = 0L, tfm = 0L, tsm = 0L, tfrm = 0L;
	VMPTR_TYPE	live = 0, disk = 0, nfreeze = 0, nifrez	= 0;
	DFREE		*ca;
	RMHEAD_PTR	sm;

	mb = VMallocated;
	if (VMbase)  {
		v = VMbase;
		tlm += RMCHUNK((VMallocated * sizeof(VMHEAD)));
		for (i=0 ; i < VMallocated  ; ++i, ++v)
			switch (v->type	& ~(MT_DIRTY | MT_FREEZE))  {
				case MT_NOTUSED:
					if (v->type)
						terror(__func__, n, "Invalid memory type");
					if (v->lru_link.i)  {
						if (v->lru_link.p.b >= mb  ||  v->lru_link.p.l >= VMLEGSIZ)
							terror(__func__, n, "Invalid lru link");
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
								terror(__func__, n, "Invalid mru link");
						}  else
							if (VMmru.p.b != b  ||	VMmru.p.l != i)
								terror(__func__, n, "Bad VMmru or mru link");
						if (v->lru_link.i)  {
							if (v->lru_link.p.b >= mb  ||  v->lru_link.p.l >= VMLEGSIZ)
								terror(__func__, n, "Invalid lru link");
						}  else
							if (VMlru.p.b != b  ||	VMlru.p.l != i)
								terror(__func__, n, "Bad VMlru or lru link");
					}
					if (v->size <= IMSIZE)
						terror(__func__, n, "Invalid size");
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
							terror(__func__, n, "Invalid disk address");
						tdm += v->size;
					}
					tvm += v->size;
					break;
				default:
					terror(__func__, n, "Invalid memory type");
					break;
			}
	}
	for (f=0, p = VMmru ; p	; p = v->lru_link)	{
		if (p >= VMallocated)
			terror(__func__, n, "Invalid pointer");
		v = &VMbase[p];
		if (!(v->type &	MT_MEMORY))
			terror(__func__, n, "Invalid memory type");
		if (v->type & MT_FREEZE)
			terror(__func__, n, "Frozen memory on lru link");
		trm2 +=	RMCHUNK(v->size);
		if (v->mru_link.i != f.i)
			terror(__func__, n, "Invalid mru link");
		f.i = p.i;
	}
	for (ca=DMflist	; ca ; ca=ca->next)
		tfm += RMCHUNK(sizeof(DFREE));
	for (sm=RMsmem ; sm ; sm=sm->s.next)
		tsm += sizeof(RMHEAD);
	if (f.i	!= VMlru.i)
		terror(__func__, n, "Invalid lru link");
	if (trm2 + tfrm	!= trm)
		terror(__func__, n, "Missing memory");
	for (p = VMfree ; p  ; p = v->lru_link)  {
		if (p >= VMallocated)
			terror(__func__, n, "Invalid free pointer");
		v = &VMbase[p];
		if (v->type)
			terror(__func__, n, "Invalid type");
	}
	if (tvm	!= VMtotal)
		terror(__func__, n, "VMtotal incorrect");
	if (live != VMlive)
		terror(__func__, n, "VMlive incorrect");
	if (disk != VMdisk)
		terror(__func__, n, "VMdisk incorrect");
	if (nfreeze != VMnfreez)
		terror(__func__, n, "VMnfreez incorrect");
	if (nifrez != VMnifrez)
		terror(__func__, n, "VMnifrez incorrect");
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

static char	*memmove(char  *t, char  *f, unsigned n)
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

#ifdef _MSC_VER

static int my_mkstemp(char *template) {
	if (_mktemp_s(template, strlen(template) + 1) != 0)
		return -1;
	return _open(template, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TRUNC | _S_IREAD | _S_IWRITE);
}

#endif

