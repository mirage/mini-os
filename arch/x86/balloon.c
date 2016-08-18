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
#include <mini-os/mm.h>
#include <mini-os/paravirt.h>

#ifdef CONFIG_BALLOON
#ifdef CONFIG_PARAVIRT
static void p2m_invalidate(unsigned long *list, unsigned long start_idx)
{
    unsigned long idx;

    for ( idx = start_idx; idx < P2M_ENTRIES; idx++ )
        list[idx] = INVALID_P2M_ENTRY;
}

static inline unsigned long *p2m_l3list(void)
{
    return mfn_to_virt(HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list);
}

static inline unsigned long *p2m_to_virt(unsigned long p2m)
{
    return ( p2m == INVALID_P2M_ENTRY ) ? NULL : mfn_to_virt(p2m);
}

void arch_remap_p2m(unsigned long max_pfn)
{
    unsigned long pfn, new_p2m;
    unsigned long *l3_list, *l2_list, *l1_list;

    l3_list = p2m_l3list();
    l2_list = p2m_to_virt(l3_list[L3_P2M_IDX(max_pfn - 1)]);
    l1_list = p2m_to_virt(l2_list[L2_P2M_IDX(max_pfn - 1)]);

    p2m_invalidate(l3_list, L3_P2M_IDX(max_pfn - 1) + 1);
    p2m_invalidate(l2_list, L2_P2M_IDX(max_pfn - 1) + 1);
    p2m_invalidate(l1_list, L1_P2M_IDX(max_pfn - 1) + 1);

    if ( p2m_pages(nr_max_pages) <= p2m_pages(max_pfn) )
        return;

    new_p2m = alloc_virt_kernel(p2m_pages(nr_max_pages));
    for ( pfn = 0; pfn < max_pfn; pfn += P2M_ENTRIES )
    {
        map_frame_rw(new_p2m + PAGE_SIZE * (pfn / P2M_ENTRIES),
                     virt_to_mfn(phys_to_machine_mapping + pfn));
    }

    phys_to_machine_mapping = (unsigned long *)new_p2m;
    printk("remapped p2m list to %p\n", phys_to_machine_mapping);
}

int arch_expand_p2m(unsigned long max_pfn)
{
    unsigned long pfn;
    unsigned long *l1_list, *l2_list, *l3_list;

    p2m_chk_pfn(max_pfn - 1);
    l3_list = p2m_l3list();

    for ( pfn = (HYPERVISOR_shared_info->arch.max_pfn + P2M_MASK) & ~P2M_MASK;
          pfn < max_pfn; pfn += P2M_ENTRIES )
    {
        l2_list = p2m_to_virt(l3_list[L3_P2M_IDX(pfn)]);
        if ( !l2_list )
        {
            l2_list = (unsigned long*)alloc_page();
            if ( !l2_list )
                return -ENOMEM;
            p2m_invalidate(l2_list, 0);
            l3_list[L3_P2M_IDX(pfn)] = virt_to_mfn(l2_list);
        }
        l1_list = p2m_to_virt(l2_list[L2_P2M_IDX(pfn)]);
        if ( !l1_list )
        {
            l1_list = (unsigned long*)alloc_page();
            if ( !l1_list )
                return -ENOMEM;
            p2m_invalidate(l1_list, 0);
            l2_list[L2_P2M_IDX(pfn)] = virt_to_mfn(l1_list);

            if ( map_frame_rw((unsigned long)(phys_to_machine_mapping + pfn),
                              l2_list[L2_P2M_IDX(pfn)]) )
                return -ENOMEM;
        }
    }

    HYPERVISOR_shared_info->arch.max_pfn = max_pfn;

    /* Make sure the new last page can be mapped. */
    if ( !need_pgt((unsigned long)pfn_to_virt(max_pfn - 1)) )
        return -ENOMEM;

    return 0;
}

void arch_pfn_add(unsigned long pfn, unsigned long mfn)
{
    mmu_update_t mmu_updates[1];
    pgentry_t *pgt;
    int rc;

    phys_to_machine_mapping[pfn] = mfn;

    pgt = need_pgt((unsigned long)pfn_to_virt(pfn));
    ASSERT(pgt);
    mmu_updates[0].ptr = virt_to_mach(pgt) | MMU_NORMAL_PT_UPDATE;
    mmu_updates[0].val = (pgentry_t)(mfn << PAGE_SHIFT) |
                         _PAGE_PRESENT | _PAGE_RW;
    rc = HYPERVISOR_mmu_update(mmu_updates, 1, NULL, DOMID_SELF);
    if ( rc < 0 )
    {
        printk("ERROR: build_pagetable(): PTE could not be updated\n");
        printk("       mmu_update failed with rc=%d\n", rc);
        do_exit();
    }
}
#else
void arch_pfn_add(unsigned long pfn, unsigned long mfn)
{
    pgentry_t *pgt;

    pgt = need_pgt((unsigned long)pfn_to_virt(pfn));
    ASSERT(pgt);
    if ( !(*pgt & _PAGE_PSE) )
        *pgt = (pgentry_t)(mfn << PAGE_SHIFT) | _PAGE_PRESENT | _PAGE_RW;
}
#endif

#endif
