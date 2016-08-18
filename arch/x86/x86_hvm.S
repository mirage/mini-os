/* Included by x86_[32|64].S */

        ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY, .long _start)
.text
        .code32                 /* Always starts in 32bit flat mode. */

.globl _start

_start:
        mov $(X86_CR4_PAE | X86_CR4_OSFXSR), %eax
        mov %eax, %cr4
        mov $page_table_base, %eax
        mov %eax, %cr3

#ifdef __x86_64__               /* EFER.LME = 1 */
        mov $MSR_EFER, %ecx
        rdmsr
        bts $_EFER_LME, %eax
        wrmsr
#endif /* __x86_64__ */

        mov %cr0, %eax
        or $X86_CR0_PG, %eax
        mov %eax, %cr0

        lgdt gdt_ptr

        /* Load code segment. */
        ljmp $__KERN_CS, $1f
#ifdef __x86_64__
        .code64
#endif

        /* Load data segments. */
1:
        mov $__USER_DS, %eax
        mov %eax, %ds
        mov %eax, %es
        mov %eax, %fs
        mov %eax, %gs
        mov $__KERN_DS, %eax
        mov %eax, %ss

        mov %ebx, %esi

.data
/*
 * Macro to create a sequence of page table entries.
 * As a loop can be done via recursion only and the nesting level is limited
 * we treat the first 32 PTEs in a special way limiting nesting level to 64
 * in case of a complete page table (512 PTEs) to be filled.
 * prot: protection bits in all PTEs
 * addr: physical address of the area to map
 * incr: increment of address for each PTE
 * idx:  index of first PTE in page table
 * end:  index of last PTE in page table + 1
 */
        .macro PTES prot, addr, incr, idx, end
        .ifgt \end-\idx-32
        PTES \prot, \addr, \incr, \idx, "(\idx+32)"
        PTES \prot, "(\addr+32*\incr)", \incr, "(\idx+32)", \end
        .else
        PTE(\addr + \prot)
        .if \end-\idx-1
        PTES \prot, "(\addr+\incr)", \incr, "(\idx+1)", \end
        .endif
        .endif
        .endm
        .align __PAGE_SIZE
page_table_virt_l1:
        PTE(0)
        .align __PAGE_SIZE, 0
page_table_l1:
        PTES L1_PROT, 0x00000000, 0x00001000, 0, L1_PAGETABLE_ENTRIES
        .align __PAGE_SIZE, 0
page_table_l2:
        /* Map the first 1GB of memory (on 32 bit 16MB less). */
        PTE(page_table_l1 + L2_PROT)
#ifdef __x86_64__
        PTES L2_PROT|_PAGE_PSE, 0x00200000, 0x00200000, 1, L2_PAGETABLE_ENTRIES
#else
        /* At 3f000000 virtual kernel area is starting. */
        PTES L2_PROT|_PAGE_PSE, 0x00200000, 0x00200000, 1, l2_table_offset(VIRT_KERNEL_AREA)
        PTE(page_table_virt_l1 + L2_PROT)
#endif
        .align __PAGE_SIZE, 0

.text
