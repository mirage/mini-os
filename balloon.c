/* -*-  Mode:C; c-basic-offset:4; tab-width:4 -*-
 *
 * (C) 2016 - Juergen Gross, SUSE Linux GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <mini-os/os.h>
#include <mini-os/balloon.h>
#include <mini-os/errno.h>
#include <mini-os/lib.h>
#include <mini-os/paravirt.h>
#include <xen/xen.h>
#include <xen/memory.h>

unsigned long nr_max_pages;
unsigned long nr_mem_pages;

void get_max_pages(void)
{
    long ret;
    domid_t domid = DOMID_SELF;

    ret = HYPERVISOR_memory_op(XENMEM_maximum_reservation, &domid);
    if ( ret < 0 )
    {
        printk("Could not get maximum pfn\n");
        return;
    }

    nr_max_pages = ret;
    printk("Maximum memory size: %ld pages\n", nr_max_pages);
}

void mm_alloc_bitmap_remap(void)
{
    unsigned long i, new_bitmap;

    if ( mm_alloc_bitmap_size >= ((nr_max_pages + 1) >> 3) )
        return;

    new_bitmap = alloc_virt_kernel(PFN_UP((nr_max_pages + 1) >> 3));
    for ( i = 0; i < mm_alloc_bitmap_size; i += PAGE_SIZE )
    {
        map_frame_rw(new_bitmap + i,
                     virt_to_mfn((unsigned long)(mm_alloc_bitmap) + i));
    }

    mm_alloc_bitmap = (unsigned long *)new_bitmap;
}

#define N_BALLOON_FRAMES 64
static unsigned long balloon_frames[N_BALLOON_FRAMES];

int balloon_up(unsigned long n_pages)
{
    unsigned long page, pfn;
    int rc;
    struct xen_memory_reservation reservation = {
        .domid        = DOMID_SELF
    };

    if ( n_pages > nr_max_pages - nr_mem_pages )
        n_pages = nr_max_pages - nr_mem_pages;
    if ( n_pages > N_BALLOON_FRAMES )
        n_pages = N_BALLOON_FRAMES;

    /* Resize alloc_bitmap if necessary. */
    while ( mm_alloc_bitmap_size * 8 < nr_mem_pages + n_pages )
    {
        page = alloc_page();
        if ( !page )
            return -ENOMEM;

        memset((void *)page, ~0, PAGE_SIZE);
        if ( map_frame_rw((unsigned long)mm_alloc_bitmap + mm_alloc_bitmap_size,
                          virt_to_mfn(page)) )
        {
            free_page((void *)page);
            return -ENOMEM;
        }

        mm_alloc_bitmap_size += PAGE_SIZE;
    }

    rc = arch_expand_p2m(nr_mem_pages + n_pages);
    if ( rc )
        return rc;

    /* Get new memory from hypervisor. */
    for ( pfn = 0; pfn < n_pages; pfn++ )
    {
        balloon_frames[pfn] = nr_mem_pages + pfn;
    }
    set_xen_guest_handle(reservation.extent_start, balloon_frames);
    reservation.nr_extents = n_pages;
    rc = HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
    if ( rc <= 0 )
        return rc;

    for ( pfn = 0; pfn < rc; pfn++ )
    {
        arch_pfn_add(nr_mem_pages + pfn, balloon_frames[pfn]);
        free_page(pfn_to_virt(nr_mem_pages + pfn));
    }

    nr_mem_pages += rc;

    return rc;
}

static int in_balloon;

int chk_free_pages(unsigned long needed)
{
    unsigned long n_pages;

    /* No need for ballooning if plenty of space available. */
    if ( needed + BALLOON_EMERGENCY_PAGES <= nr_free_pages )
        return 1;

    /* If we are already ballooning up just hope for the best. */
    if ( in_balloon )
        return 1;

    /* Interrupts disabled can't be handled right now. */
    if ( irqs_disabled() )
        return 1;

    in_balloon = 1;

    while ( needed + BALLOON_EMERGENCY_PAGES > nr_free_pages )
    {
        n_pages = needed + BALLOON_EMERGENCY_PAGES - nr_free_pages;
        if ( !balloon_up(n_pages) )
            break;
    }

    in_balloon = 0;

    return needed <= nr_free_pages;
}
