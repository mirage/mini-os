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
#include <mini-os/lib.h>
#include <mini-os/mm.h>

#ifdef CONFIG_BALLOON

unsigned long virt_kernel_area_end = VIRT_KERNEL_AREA;

void arch_remap_p2m(unsigned long max_pfn)
{
    unsigned long pfn;

    if ( p2m_pages(nr_max_pages) <= p2m_pages(max_pfn) )
        return;

    for ( pfn = 0; pfn < max_pfn; pfn += P2M_ENTRIES )
    {
        map_frame_rw(virt_kernel_area_end + PAGE_SIZE * (pfn / P2M_ENTRIES),
                     virt_to_mfn(phys_to_machine_mapping + pfn));
    }

    phys_to_machine_mapping = (unsigned long *)virt_kernel_area_end;
    printk("remapped p2m list to %p\n", phys_to_machine_mapping);

    virt_kernel_area_end += PAGE_SIZE * p2m_pages(nr_max_pages);
    ASSERT(virt_kernel_area_end <= VIRT_DEMAND_AREA);
}

#endif
