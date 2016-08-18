#ifndef _X86_ASM_MACRO_H_
#define _X86_ASM_MACRO_H_

#ifdef  __ASSEMBLY__
# if defined(__x86_64__)
#  define _WORD .quad
# elif defined(__i386__)
#  define _WORD .long
# endif
#else
# if defined(__x86_64__)
#  define _WORD ".quad"
# elif defined(__i386__)
#  define _WORD ".long"
# endif
#endif

#endif	/* _X86_ASM_MACRO_H_ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
