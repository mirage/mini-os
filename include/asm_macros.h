/*
 * Macros for assembly files.
 */

#ifndef _ASM_MACRO_H_
#define _ASM_MACRO_H_

#if defined(__i386__) || defined(__x86_64__)
#include <x86/asm_macros.h>
#elif defined(__arm__) || defined(__aarch64__)
#include <arm/asm_macros.h>
#endif

#ifdef __ASSEMBLY__

#define ELFNOTE(name, type, desc)           \
    .pushsection .note.name               ; \
    .align 4                              ; \
    .long 2f - 1f       /* namesz */      ; \
    .long 4f - 3f       /* descsz */      ; \
    .long type          /* type   */      ; \
1:.asciz #name          /* name   */      ; \
2:.align 4                                ; \
3:desc                  /* desc   */      ; \
4:.align 4                                ; \
    .popsection

#endif  /* __ASSEMBLY__ */

#endif  /* _ASM_MACRO_H_ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
