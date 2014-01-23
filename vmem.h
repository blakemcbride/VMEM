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
 *	 	See the "copyrite" file for usage terms.
*/

/*  
    Currently setup for MicroSoft C and Turbo C and UNIX - they handle huge 
    pointers differently and have different huge allocation functions.
*/


#if  !defined(__TURBOC__)  &&  !defined(unix)
#define	 MSC	/* MicroSoft C 4.0 and above	*/
#endif


#include <stdio.h>
#if !defined(unix)
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

#if !defined(__TURBOC__)  &&  !defined(MSC)
#define	USE_MALLOC
#endif

#ifndef OPEN
#define OPEN		open
#endif
#define	STATIC		static

#ifndef	DEBUG
#define	TEST(f,n)
#endif

#define FNAME(x)	static char fun[] = #x

#if defined(__TURBOC__)  ||  defined(MSC)
#define HUGE huge
#else
#define	HUGE
#endif

#ifdef __TURBOC__
#define NO_ALLOCA 1024
#endif

#ifdef	unix
#define	NO_ALLOCA 4096
#define	NO_ATEXIT
#define	NO_MEMMOVE
#endif

#ifdef	MSC

#define	HPtoL(p)  (((unsigned long)FP_SEG(p)<<4) + (unsigned long)FP_OFF(p))
STATIC	char HUGE   *HPtoFP();	/*  don't use this with OS/2 (use the #else)  */
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

typedef	unsigned char	LEG_TYPE;
typedef	unsigned char	BASE_TYPE;
typedef	unsigned short	VMPTR_TYPE;   /*  large	enough to hold LEG_TYPE	and BASE_TYPE  */
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

void	rmfree(), vmfree_head(), VM_free(), compact(), page_out(), dfree_free(), vm_link(), vm_unlink();
void	d_compact(), VM_end(), error(),	VM_dcmps(), d_compact1(), d_compact2(),	rest_clean();
long	disk_next();
DFREE	HUGE *dfree_new();

extern	long	lseek();


