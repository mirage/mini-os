#include <mini-os/os.h>
#include <mini-os/kernel.h>
#include <mini-os/gic.h>
#include <mini-os/console.h>
#include <xen/xen.h>
#include <xen/memory.h>
#include <xen/hvm/params.h>
#include <arch_mm.h>
#include <libfdt.h>

/*
 * Shared page for communicating with the hypervisor.
 * Events flags go here, for example.
 */
shared_info_t *HYPERVISOR_shared_info;

void *device_tree;

/*
 * INITIAL C ENTRY POINT.
 */
void arch_init(void *dtb_pointer, uint32_t physical_offset)
{
    int r;

    memset(&__bss_start, 0, &_end - &__bss_start);

    physical_address_offset = physical_offset;

    xprintk("Virtual -> physical offset = %x\n", physical_address_offset);

    xprintk("Checking DTB at %p...\n", dtb_pointer);

    if ((r = fdt_check_header(dtb_pointer))) {
        xprintk("Invalid DTB from Xen: %s\n", fdt_strerror(r));
        BUG();
    }
    device_tree = dtb_pointer;

    /* Map shared_info page */
    HYPERVISOR_shared_info = map_shared_info(NULL);

    get_console(NULL);
    get_xenbus(NULL);

    gic_init();

    start_kernel();
}

void
arch_fini(void)
{
}

void
arch_do_exit(void)
{
}
