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
 *             blake@mcbridemail.com
 *
 *
 *	 	See the "License.txt" file for usage terms.
*/


/* VMPTR_TYPE is now just an index into a linear array */
#ifdef __LP64__
  typedef	unsigned int	VMPTR_TYPE;   /* 32-bit index - allows up to 4 billion objects */
#else
  typedef	unsigned short	VMPTR_TYPE;   /* 16-bit index - allows up to 65k objects */
#endif


extern int	VM_newadd;			/* set to 1 when memory changed	*/

int		VM_init(void);
VMPTR_TYPE	VM_alloc(long s, int zero);
void		VM_free(VMPTR_TYPE i);
void		VM_end(void), VM_dcmps(void);
long		*VM_stat(void);
void		VM_parm(long rmmax, long rmasize, double rmcompf, long dmmfree, int dmmfblks, int dmctype);
void		VM_fcore(void);
void		*VM_addr(VMPTR_TYPE i, int dirty, int frez);
VMPTR_TYPE	VM_realloc(VMPTR_TYPE i, long s);
int		VM_dump(char *f);
int		VM_rest(char *f);
int		VM_frest(char *f);



