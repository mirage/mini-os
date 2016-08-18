#if defined(__x86_64__)

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

#elif defined(__i386__)
#undef i386
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)

#else
# error Bad architecture to link with
#endif

ENTRY(_start)
SECTIONS
{
        . = 0x0;
        _text = .;			/* Text and read-only data */
        .text : {
                *(.text)
                *(.gnu.warning)
        } = 0x9090

        _etext = .;			/* End of text section */

        .rodata : {
                *(.rodata)
                *(.rodata.*)
        }
        . = ALIGN(4096);
        _erodata = .;

        .note : {
                *(.note)
                *(.note.*)
        }

        /* newlib initialization functions */
#if defined(__x86_64__)
        . = ALIGN(64 / 8);
#else /* __i386 __ */
        . = ALIGN(32 / 8);
#endif
        PROVIDE (__preinit_array_start = .);
        .preinit_array : {
                *(.preinit_array)
        }
        PROVIDE (__preinit_array_end = .);
        PROVIDE (__init_array_start = .);
        .init_array : {
                *(.init_array)
        }
        PROVIDE (__init_array_end = .);
        PROVIDE (__fini_array_start = .);
        .fini_array : {
                *(.fini_array)
        }
        PROVIDE (__fini_array_end = .);

        .ctors : {
                __CTOR_LIST__ = .;
                *(.ctors)
                CONSTRUCTORS
#if defined(__x86_64__)
                QUAD(0)
#else /* __i386__ */
                LONG(0)
#endif
                __CTOR_END__ = .;
        }

        .dtors : {
                __DTOR_LIST__ = .;
                *(.dtors)
#if defined(__x86_64__)
                QUAD(0)
#else /* __i386__ */
                LONG(0)
#endif
                __DTOR_END__ = .;
        }

        .data : {			/* Data */
                *(.data)
        }

        _edata = .;			/* End of data section */

        __bss_start = .;		/* BSS */
        .bss : {
                *(.bss)
                *(.app.bss)
        }
        _end = . ;

        /* Sections to be discarded */
        /DISCARD/ : {
                *(.text.exit)
                *(.data.exit)
                *(.exitcall.exit)
        }

        /* Stabs debugging sections.  */
        .stab 0 : {
                *(.stab)
        }
        .stabstr 0 : {
                *(.stabstr)
        }
        .stab.excl 0 : {
                *(.stab.excl)
        }
        .stab.exclstr 0 : {
                *(.stab.exclstr)
        }
        .stab.index 0 : {
                *(.stab.index)
        }
        .stab.indexstr 0 : {
                *(.stab.indexstr)
        }
        .comment 0 : {
                *(.comment)
        }
}

/*
 * Local variables:
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 */
