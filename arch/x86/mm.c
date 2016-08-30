/* 
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: mm.c
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: Grzegorz Milos
 *              
 *        Date: Aug 2003, chages Aug 2005
 * 
 * Environment: Xen Minimal OS
 * Description: memory management related functions
 *              contains buddy page allocator from Xen.
 *
 ****************************************************************************
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

#include <mini-os/errno.h>
#include <mini-os/os.h>
#include <mini-os/hypervisor.h>
#include <mini-os/balloon.h>
#include <mini-os/mm.h>
#include <mini-os/paravirt.h>
#include <mini-os/types.h>
#include <mini-os/lib.h>
#include <mini-os/xmalloc.h>
#include <mini-os/e820.h>
#include <xen/memory.h>

#ifdef MM_DEBUG
#define DEBUG(_f, _a...) \
    printk("MINI_OS(file=mm.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

unsigned long *phys_to_machine_mapping;
unsigned long mfn_zero;
pgentry_t *pt_base;
static unsigned long first_free_pfn;
static unsigned long last_free_pfn;
static unsigned long virt_kernel_area_end = VIRT_KERNEL_AREA;

extern char stack[];
extern void page_walk(unsigned long va);

#ifdef CONFIG_PARAVIRT
struct e820entry e820_map[1] = {
    {
        .addr = 0,
        .size = ULONG_MAX - 1,
        .type = E820_RAM
    }
};
unsigned e820_entries = 1;

void arch_mm_preinit(void *p)
{
    start_info_t *si = p;

    phys_to_machine_mapping = (unsigned long *)si->mfn_list;
    pt_base = (pgentry_t *)si->pt_base;
    first_free_pfn = PFN_UP(to_phys(pt_base)) + si->nr_pt_frames;
    last_free_pfn = si->nr_pages;
}
#else
#include <mini-os/desc.h>
user_desc gdt[NR_GDT_ENTRIES] =
{
    [GDTE_CS64_DPL0] = INIT_GDTE_SYM(0, 0xfffff, COMMON, CODE, DPL0, R, L),
    [GDTE_CS32_DPL0] = INIT_GDTE_SYM(0, 0xfffff, COMMON, CODE, DPL0, R, D),
    [GDTE_DS32_DPL0] = INIT_GDTE_SYM(0, 0xfffff, COMMON, DATA, DPL0, B, W),

    [GDTE_CS64_DPL3] = INIT_GDTE_SYM(0, 0xfffff, COMMON, CODE, DPL3, R, L),
    [GDTE_CS32_DPL3] = INIT_GDTE_SYM(0, 0xfffff, COMMON, CODE, DPL3, R, D),
    [GDTE_DS32_DPL3] = INIT_GDTE_SYM(0, 0xfffff, COMMON, DATA, DPL3, B, W),

    /* [GDTE_TSS]     */
    /* [GDTE_TSS + 1] */
};

desc_ptr gdt_ptr =
{
    .limit = sizeof(gdt) - 1,
    .base = (unsigned long)&gdt,
};

gate_desc idt[256] = { };

desc_ptr idt_ptr =
{
    .limit = sizeof(idt) - 1,
    .base = (unsigned long)&idt,
};

struct e820entry e820_map[E820_MAX];
unsigned e820_entries;

static char *e820_types[E820_TYPES] = {
    [E820_RAM]      = "RAM",
    [E820_RESERVED] = "Reserved",
    [E820_ACPI]     = "ACPI",
    [E820_NVS]      = "NVS",
    [E820_UNUSABLE] = "Unusable",
    [E820_PMEM]     = "PMEM"
};

void arch_mm_preinit(void *p)
{
    long ret;
    domid_t domid = DOMID_SELF;
    struct xen_memory_map memmap;
    int i;
    unsigned long pfn, max = 0;

    pt_base = page_table_base;
    first_free_pfn = PFN_UP(to_phys(&_end));
    ret = HYPERVISOR_memory_op(XENMEM_current_reservation, &domid);
    if ( ret < 0 )
    {
        xprintk("could not get memory size\n");
        do_exit();
    }
    last_free_pfn = ret;

    memmap.nr_entries = E820_MAX;
    set_xen_guest_handle(memmap.buffer, e820_map);
    ret = HYPERVISOR_memory_op(XENMEM_memory_map, &memmap);
    if ( ret < 0 )
    {
        xprintk("could not get memory map\n");
        do_exit();
    }
    e820_entries = memmap.nr_entries;

    for ( i = 0; i < e820_entries; i++ )
    {
        if ( e820_map[i].type != E820_RAM )
            continue;
        pfn = (e820_map[i].addr + e820_map[i].size) >> PAGE_SHIFT;
        if ( pfn > max )
            max = pfn;
    }

    if ( max < last_free_pfn )
        last_free_pfn = max;
}

void arch_print_memmap(void)
{
    int i;
    unsigned long from, to;
    char *type;
    char buf[12];

    printk("Memory map:\n");
    for ( i = 0; i < e820_entries; i++ )
    {
        if ( e820_map[i].type >= E820_TYPES || !e820_types[e820_map[i].type] )
        {
            snprintf(buf, sizeof(buf), "%8x", e820_map[i].type);
            type = buf;
        }
        else
        {
            type = e820_types[e820_map[i].type];
        }
        from = e820_map[i].addr;
        to = from + e820_map[i].size - 1;
        printk("%012lx-%012lx: %s\n", from, to, type);
    }
}
#endif

/*
 * Make pt_pfn a new 'level' page table frame and hook it into the page
 * table at offset in previous level MFN (pref_l_mfn). pt_pfn is a guest
 * PFN.
 */
static pgentry_t pt_prot[PAGETABLE_LEVELS] = {
    L1_PROT,
    L2_PROT,
    L3_PROT,
#if defined(__x86_64__)
    L4_PROT,
#endif
};

static void new_pt_frame(unsigned long *pt_pfn, unsigned long prev_l_mfn, 
                         unsigned long offset, unsigned long level)
{   
    pgentry_t *tab;
    unsigned long pt_page = (unsigned long)pfn_to_virt(*pt_pfn); 
#ifdef CONFIG_PARAVIRT
    mmu_update_t mmu_updates[1];
    int rc;
#endif
    
    DEBUG("Allocating new L%d pt frame for pfn=%lx, "
          "prev_l_mfn=%lx, offset=%lx", 
          level, *pt_pfn, prev_l_mfn, offset);

    /* We need to clear the page, otherwise we might fail to map it
       as a page table page */
    memset((void*) pt_page, 0, PAGE_SIZE);  

    ASSERT(level >= 1 && level <= PAGETABLE_LEVELS);

#ifdef CONFIG_PARAVIRT
    /* Make PFN a page table page */
    tab = pt_base;
#if defined(__x86_64__)
    tab = pte_to_virt(tab[l4_table_offset(pt_page)]);
#endif
    tab = pte_to_virt(tab[l3_table_offset(pt_page)]);

    mmu_updates[0].ptr = (tab[l2_table_offset(pt_page)] & PAGE_MASK) + 
        sizeof(pgentry_t) * l1_table_offset(pt_page);
    mmu_updates[0].val = (pgentry_t)pfn_to_mfn(*pt_pfn) << PAGE_SHIFT | 
        (pt_prot[level - 1] & ~_PAGE_RW);
    
    if ( (rc = HYPERVISOR_mmu_update(mmu_updates, 1, NULL, DOMID_SELF)) < 0 )
    {
        printk("ERROR: PTE for new page table page could not be updated\n");
        printk("       mmu_update failed with rc=%d\n", rc);
        do_exit();
    }

    /* Hook the new page table page into the hierarchy */
    mmu_updates[0].ptr =
        ((pgentry_t)prev_l_mfn << PAGE_SHIFT) + sizeof(pgentry_t) * offset;
    mmu_updates[0].val = (pgentry_t)pfn_to_mfn(*pt_pfn) << PAGE_SHIFT |
        pt_prot[level];

    if ( (rc = HYPERVISOR_mmu_update(mmu_updates, 1, NULL, DOMID_SELF)) < 0 ) 
    {
        printk("ERROR: mmu_update failed with rc=%d\n", rc);
        do_exit();
    }
#else
    tab = mfn_to_virt(prev_l_mfn);
    tab[offset] = (*pt_pfn << PAGE_SHIFT) | pt_prot[level];
#endif

    *pt_pfn += 1;
}

/*
 * Build the initial pagetable.
 */
static void build_pagetable(unsigned long *start_pfn, unsigned long *max_pfn)
{
    unsigned long start_address, end_address;
    unsigned long pfn_to_map, pt_pfn = *start_pfn;
    pgentry_t *tab = pt_base, page;
    unsigned long pt_mfn = pfn_to_mfn(virt_to_pfn(pt_base));
    unsigned long offset;
#ifdef CONFIG_PARAVIRT
    static mmu_update_t mmu_updates[L1_PAGETABLE_ENTRIES + 1];
    int count = 0;
    int rc;
#endif

    /* Be conservative: even if we know there will be more pages already
       mapped, start the loop at the very beginning. */
    pfn_to_map = *start_pfn;

#ifdef CONFIG_PARAVIRT
    if ( *max_pfn >= virt_to_pfn(HYPERVISOR_VIRT_START) )
    {
        printk("WARNING: Mini-OS trying to use Xen virtual space. "
               "Truncating memory from %luMB to ",
               ((unsigned long)pfn_to_virt(*max_pfn) -
                (unsigned long)&_text)>>20);
        *max_pfn = virt_to_pfn(HYPERVISOR_VIRT_START - PAGE_SIZE);
        printk("%luMB\n",
               ((unsigned long)pfn_to_virt(*max_pfn) - 
                (unsigned long)&_text)>>20);
    }
#else
    /* Round up to next 2MB boundary as we are using 2MB pages on HVMlite. */
    pfn_to_map = (pfn_to_map + L1_PAGETABLE_ENTRIES - 1) &
                 ~(L1_PAGETABLE_ENTRIES - 1);
#endif

    start_address = (unsigned long)pfn_to_virt(pfn_to_map);
    end_address = (unsigned long)pfn_to_virt(*max_pfn);

    /* We worked out the virtual memory range to map, now mapping loop */
    printk("Mapping memory range 0x%lx - 0x%lx\n", start_address, end_address);

    while ( start_address < end_address )
    {
        tab = pt_base;
        pt_mfn = pfn_to_mfn(virt_to_pfn(pt_base));

#if defined(__x86_64__)
        offset = l4_table_offset(start_address);
        /* Need new L3 pt frame */
        if ( !(tab[offset] & _PAGE_PRESENT) )
            new_pt_frame(&pt_pfn, pt_mfn, offset, L3_FRAME);

        page = tab[offset];
        pt_mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(pt_mfn) << PAGE_SHIFT);
#endif
        offset = l3_table_offset(start_address);
        /* Need new L2 pt frame */
        if ( !(tab[offset] & _PAGE_PRESENT) )
            new_pt_frame(&pt_pfn, pt_mfn, offset, L2_FRAME);

        page = tab[offset];
        pt_mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(pt_mfn) << PAGE_SHIFT);
        offset = l2_table_offset(start_address);        
#ifdef CONFIG_PARAVIRT
        /* Need new L1 pt frame */
        if ( !(tab[offset] & _PAGE_PRESENT) )
            new_pt_frame(&pt_pfn, pt_mfn, offset, L1_FRAME);

        page = tab[offset];
        pt_mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(pt_mfn) << PAGE_SHIFT);
        offset = l1_table_offset(start_address);

        if ( !(tab[offset] & _PAGE_PRESENT) )
        {
            mmu_updates[count].ptr =
                ((pgentry_t)pt_mfn << PAGE_SHIFT) + sizeof(pgentry_t) * offset;
            mmu_updates[count].val =
                (pgentry_t)pfn_to_mfn(pfn_to_map) << PAGE_SHIFT | L1_PROT;
            count++;
        }
        pfn_to_map++;
        if ( count == L1_PAGETABLE_ENTRIES ||
             (count && pfn_to_map == *max_pfn) )
        {
            rc = HYPERVISOR_mmu_update(mmu_updates, count, NULL, DOMID_SELF);
            if ( rc < 0 )
            {
                printk("ERROR: build_pagetable(): PTE could not be updated\n");
                printk("       mmu_update failed with rc=%d\n", rc);
                do_exit();
            }
            count = 0;
        }
        start_address += PAGE_SIZE;
#else
        if ( !(tab[offset] & _PAGE_PRESENT) )
            tab[offset] = (pgentry_t)pfn_to_map << PAGE_SHIFT |
                          L2_PROT | _PAGE_PSE;
        start_address += 1UL << L2_PAGETABLE_SHIFT;
#endif
    }

    *start_pfn = pt_pfn;
}

/*
 * Mark portion of the address space read only.
 */
extern struct shared_info shared_info;
static void set_readonly(void *text, void *etext)
{
    unsigned long start_address =
        ((unsigned long) text + PAGE_SIZE - 1) & PAGE_MASK;
    unsigned long end_address = (unsigned long) etext;
    pgentry_t *tab = pt_base, page;
    unsigned long mfn = pfn_to_mfn(virt_to_pfn(pt_base));
    unsigned long offset;
    unsigned long page_size = PAGE_SIZE;
#ifdef CONFIG_PARAVIRT
    static mmu_update_t mmu_updates[L1_PAGETABLE_ENTRIES + 1];
    int count = 0;
    int rc;
#endif

    printk("setting %p-%p readonly\n", text, etext);

    while ( start_address + page_size <= end_address )
    {
        tab = pt_base;
        mfn = pfn_to_mfn(virt_to_pfn(pt_base));

#if defined(__x86_64__)
        offset = l4_table_offset(start_address);
        page = tab[offset];
        mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(mfn) << PAGE_SHIFT);
#endif
        offset = l3_table_offset(start_address);
        page = tab[offset];
        mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(mfn) << PAGE_SHIFT);
        offset = l2_table_offset(start_address);        
        if ( !(tab[offset] & _PAGE_PSE) )
        {
            page = tab[offset];
            mfn = pte_to_mfn(page);
            tab = to_virt(mfn_to_pfn(mfn) << PAGE_SHIFT);

            offset = l1_table_offset(start_address);
        }

        if ( start_address != (unsigned long)&shared_info )
        {
#ifdef CONFIG_PARAVIRT
            mmu_updates[count].ptr = 
                ((pgentry_t)mfn << PAGE_SHIFT) + sizeof(pgentry_t) * offset;
            mmu_updates[count].val = tab[offset] & ~_PAGE_RW;
            count++;
#else
            tab[offset] &= ~_PAGE_RW;
#endif
        }
        else
            printk("skipped %lx\n", start_address);

        start_address += page_size;

#ifdef CONFIG_PARAVIRT
        if ( count == L1_PAGETABLE_ENTRIES || 
             start_address + page_size > end_address )
        {
            rc = HYPERVISOR_mmu_update(mmu_updates, count, NULL, DOMID_SELF);
            if ( rc < 0 )
            {
                printk("ERROR: set_readonly(): PTE could not be updated\n");
                do_exit();
            }
            count = 0;
        }
#else
        if ( start_address == (1UL << L2_PAGETABLE_SHIFT) )
            page_size = 1UL << L2_PAGETABLE_SHIFT;
#endif
    }

#ifdef CONFIG_PARAVIRT
    {
        mmuext_op_t op = {
            .cmd = MMUEXT_TLB_FLUSH_ALL,
        };
        int count;
        HYPERVISOR_mmuext_op(&op, 1, &count, DOMID_SELF);
    }
#else
    write_cr3((unsigned long)pt_base);
#endif
}

/*
 * get the PTE for virtual address va if it exists. Otherwise NULL.
 */
static pgentry_t *get_pgt(unsigned long va)
{
    unsigned long mfn;
    pgentry_t *tab;
    unsigned offset;

    tab = pt_base;
    mfn = virt_to_mfn(pt_base);

#if defined(__x86_64__)
    offset = l4_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) )
        return NULL;
    mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(mfn);
#endif
    offset = l3_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) )
        return NULL;
    mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(mfn);
    offset = l2_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) )
        return NULL;
    if ( tab[offset] & _PAGE_PSE )
        return &tab[offset];
    mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(mfn);
    offset = l1_table_offset(va);
    return &tab[offset];
}


/*
 * return a valid PTE for a given virtual address. If PTE does not exist,
 * allocate page-table pages.
 */
pgentry_t *need_pgt(unsigned long va)
{
    unsigned long pt_mfn;
    pgentry_t *tab;
    unsigned long pt_pfn;
    unsigned offset;

    tab = pt_base;
    pt_mfn = virt_to_mfn(pt_base);

#if defined(__x86_64__)
    offset = l4_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) )
    {
        pt_pfn = virt_to_pfn(alloc_page());
        if ( !pt_pfn )
            return NULL;
        new_pt_frame(&pt_pfn, pt_mfn, offset, L3_FRAME);
    }
    ASSERT(tab[offset] & _PAGE_PRESENT);
    pt_mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(pt_mfn);
#endif
    offset = l3_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) ) 
    {
        pt_pfn = virt_to_pfn(alloc_page());
        if ( !pt_pfn )
            return NULL;
        new_pt_frame(&pt_pfn, pt_mfn, offset, L2_FRAME);
    }
    ASSERT(tab[offset] & _PAGE_PRESENT);
    pt_mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(pt_mfn);
    offset = l2_table_offset(va);
    if ( !(tab[offset] & _PAGE_PRESENT) )
    {
        pt_pfn = virt_to_pfn(alloc_page());
        if ( !pt_pfn )
            return NULL;
        new_pt_frame(&pt_pfn, pt_mfn, offset, L1_FRAME);
    }
    ASSERT(tab[offset] & _PAGE_PRESENT);
    if ( tab[offset] & _PAGE_PSE )
        return &tab[offset];

    pt_mfn = pte_to_mfn(tab[offset]);
    tab = mfn_to_virt(pt_mfn);

    offset = l1_table_offset(va);
    return &tab[offset];
}

/*
 * Reserve an area of virtual address space for mappings and Heap
 */
static unsigned long demand_map_area_start;
static unsigned long demand_map_area_end;
#ifdef HAVE_LIBC
unsigned long heap, brk, heap_mapped, heap_end;
#endif

void arch_init_demand_mapping_area(void)
{
    demand_map_area_start = VIRT_DEMAND_AREA;
    demand_map_area_end = demand_map_area_start + DEMAND_MAP_PAGES * PAGE_SIZE;
    printk("Demand map pfns at %lx-%lx.\n", demand_map_area_start,
           demand_map_area_end);

#ifdef HAVE_LIBC
    heap_mapped = brk = heap = VIRT_HEAP_AREA;
    heap_end = heap_mapped + HEAP_PAGES * PAGE_SIZE;
    printk("Heap resides at %lx-%lx.\n", brk, heap_end);
#endif
}

unsigned long allocate_ondemand(unsigned long n, unsigned long alignment)
{
    unsigned long x;
    unsigned long y = 0;

    /* Find a properly aligned run of n contiguous frames */
    for ( x = 0;
          x <= DEMAND_MAP_PAGES - n; 
          x = (x + y + 1 + alignment - 1) & ~(alignment - 1) )
    {
        unsigned long addr = demand_map_area_start + x * PAGE_SIZE;
        pgentry_t *pgt = get_pgt(addr);
        for ( y = 0; y < n; y++, addr += PAGE_SIZE ) 
        {
            if ( !(addr & L1_MASK) )
                pgt = get_pgt(addr);
            if ( pgt )
            {
                if ( *pgt & _PAGE_PRESENT )
                    break;
                pgt++;
            }
        }
        if ( y == n )
            break;
    }
    if ( y != n )
    {
        printk("Failed to find %ld frames!\n", n);
        return 0;
    }
    return demand_map_area_start + x * PAGE_SIZE;
}

/*
 * Map an array of MFNs contiguously into virtual address space starting at
 * va. map f[i*stride]+i*increment for i in 0..n-1.
 */
#define MAP_BATCH ((STACK_SIZE / 2) / sizeof(mmu_update_t))
int do_map_frames(unsigned long va,
                  const unsigned long *mfns, unsigned long n,
                  unsigned long stride, unsigned long incr,
                  domid_t id, int *err, unsigned long prot)
{
    pgentry_t *pgt = NULL;
    unsigned long done = 0;

    if ( !mfns ) 
    {
        printk("do_map_frames: no mfns supplied\n");
        return -EINVAL;
    }
    DEBUG("va=%p n=0x%lx, mfns[0]=0x%lx stride=0x%lx incr=0x%lx prot=0x%lx\n",
          va, n, mfns[0], stride, incr, prot);

    if ( err )
        memset(err, 0x00, n * sizeof(int));
    while ( done < n )
    {
#ifdef CONFIG_PARAVIRT
        unsigned long i;
        int rc;
        unsigned long todo;

        if ( err )
            todo = 1;
        else
            todo = n - done;

        if ( todo > MAP_BATCH )
            todo = MAP_BATCH;

        {
            mmu_update_t mmu_updates[todo];

            for ( i = 0; i < todo; i++, va += PAGE_SIZE, pgt++) 
            {
                if ( !pgt || !(va & L1_MASK) )
                    pgt = need_pgt(va);
                if ( !pgt )
                    return -ENOMEM;

                mmu_updates[i].ptr = virt_to_mach(pgt) | MMU_NORMAL_PT_UPDATE;
                mmu_updates[i].val = ((pgentry_t)(mfns[(done + i) * stride] +
                                                  (done + i) * incr)
                                      << PAGE_SHIFT) | prot;
            }

            rc = HYPERVISOR_mmu_update(mmu_updates, todo, NULL, id);
            if ( rc < 0 )
            {
                if (err)
                    err[done * stride] = rc;
                else {
                    printk("Map %ld (%lx, ...) at %lx failed: %d.\n",
                           todo, mfns[done * stride] + done * incr, va, rc);
                    do_exit();
                }
            }
        }
        done += todo;
#else
        if ( !pgt || !(va & L1_MASK) )
            pgt = need_pgt(va & ~L1_MASK);
        if ( !pgt )
            return -ENOMEM;

        ASSERT(!(*pgt & _PAGE_PSE));
        pgt[l1_table_offset(va)] = (pgentry_t)
            (((mfns[done * stride] + done * incr) << PAGE_SHIFT) | prot);
        done++;
#endif
    }

    return 0;
}

/*
 * Map an array of MFNs contiguous into virtual address space. Virtual
 * addresses are allocated from the on demand area.
 */
void *map_frames_ex(const unsigned long *mfns, unsigned long n, 
                    unsigned long stride, unsigned long incr,
                    unsigned long alignment,
                    domid_t id, int *err, unsigned long prot)
{
    unsigned long va = allocate_ondemand(n, alignment);

    if ( !va )
        return NULL;

    if ( do_map_frames(va, mfns, n, stride, incr, id, err, prot) )
        return NULL;

    return (void *)va;
}

/*
 * Unmap nun_frames frames mapped at virtual address va.
 */
#define UNMAP_BATCH ((STACK_SIZE / 2) / sizeof(multicall_entry_t))
int unmap_frames(unsigned long va, unsigned long num_frames)
{
#ifdef CONFIG_PARAVIRT
    int n = UNMAP_BATCH;
    multicall_entry_t call[n];
    int ret;
    int i;
#else
    pgentry_t *pgt;
#endif

    ASSERT(!((unsigned long)va & ~PAGE_MASK));

    DEBUG("va=%p, num=0x%lx\n", va, num_frames);

    while ( num_frames ) {
#ifdef CONFIG_PARAVIRT
        if ( n > num_frames )
            n = num_frames;

        for ( i = 0; i < n; i++ )
        {
            int arg = 0;
            /* simply update the PTE for the VA and invalidate TLB */
            call[i].op = __HYPERVISOR_update_va_mapping;
            call[i].args[arg++] = va;
            call[i].args[arg++] = 0;
#ifdef __i386__
            call[i].args[arg++] = 0;
#endif  
            call[i].args[arg++] = UVMF_INVLPG;

            va += PAGE_SIZE;
        }

        ret = HYPERVISOR_multicall(call, n);
        if ( ret )
        {
            printk("update_va_mapping hypercall failed with rc=%d.\n", ret);
            return -ret;
        }

        for ( i = 0; i < n; i++ )
        {
            if ( call[i].result ) 
            {
                printk("update_va_mapping failed for with rc=%d.\n", ret);
                return -(call[i].result);
            }
        }
        num_frames -= n;
#else
        pgt = get_pgt(va);
        if ( pgt )
        {
            ASSERT(!(*pgt & _PAGE_PSE));
            *pgt = 0;
            invlpg(va);
        }
        va += PAGE_SIZE;
        num_frames--;
#endif
    }
    return 0;
}

/*
 * Clear some of the bootstrap memory
 */
static void clear_bootstrap(void)
{
#ifdef CONFIG_PARAVIRT
    pte_t nullpte = { };
    int rc;
#else
    pgentry_t *pgt;
#endif

    /* Use first page as the CoW zero page */
    memset(&_text, 0, PAGE_SIZE);
    mfn_zero = virt_to_mfn((unsigned long) &_text);
#ifdef CONFIG_PARAVIRT
    if ( (rc = HYPERVISOR_update_va_mapping(0, nullpte, UVMF_INVLPG)) )
        printk("Unable to unmap NULL page. rc=%d\n", rc);
#else
    pgt = get_pgt((unsigned long)&_text);
    *pgt = 0;
    invlpg((unsigned long)&_text);
#endif
}

#ifdef CONFIG_PARAVIRT
void p2m_chk_pfn(unsigned long pfn)
{
    if ( (pfn >> L3_P2M_SHIFT) > 0 )
    {
        printk("Error: Too many pfns.\n");
        do_exit();
    }
}

void arch_init_p2m(unsigned long max_pfn)
{
    unsigned long *l2_list = NULL, *l3_list;
    unsigned long pfn;
    
    p2m_chk_pfn(max_pfn - 1);
    l3_list = (unsigned long *)alloc_page(); 
    for ( pfn = 0; pfn < max_pfn; pfn += P2M_ENTRIES )
    {
        if ( !(pfn % (P2M_ENTRIES * P2M_ENTRIES)) )
        {
            l2_list = (unsigned long*)alloc_page();
            l3_list[L3_P2M_IDX(pfn)] = virt_to_mfn(l2_list);
        }
        l2_list[L2_P2M_IDX(pfn)] = virt_to_mfn(phys_to_machine_mapping + pfn);
    }
    HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list = 
        virt_to_mfn(l3_list);
    HYPERVISOR_shared_info->arch.max_pfn = max_pfn;

    arch_remap_p2m(max_pfn);
}
#endif

void arch_init_mm(unsigned long* start_pfn_p, unsigned long* max_pfn_p)
{
    unsigned long start_pfn, max_pfn;

    printk("      _text: %p(VA)\n", &_text);
    printk("     _etext: %p(VA)\n", &_etext);
    printk("   _erodata: %p(VA)\n", &_erodata);
    printk("     _edata: %p(VA)\n", &_edata);
    printk("stack start: %p(VA)\n", stack);
    printk("       _end: %p(VA)\n", &_end);

    /* First page follows page table pages. */
    start_pfn = first_free_pfn;
    max_pfn = last_free_pfn;

    if ( max_pfn >= MAX_MEM_SIZE / PAGE_SIZE )
        max_pfn = MAX_MEM_SIZE / PAGE_SIZE - 1;

    printk("  start_pfn: %lx\n", start_pfn);
    printk("    max_pfn: %lx\n", max_pfn);

    build_pagetable(&start_pfn, &max_pfn);
    clear_bootstrap();
    set_readonly(&_text, &_erodata);

    *start_pfn_p = start_pfn;
    *max_pfn_p = max_pfn;

#ifndef CONFIG_PARAVIRT
#ifdef __x86_64__
    BUILD_BUG_ON(l4_table_offset(VIRT_KERNEL_AREA) != 1 ||
                 l3_table_offset(VIRT_KERNEL_AREA) != 0 ||
                 l2_table_offset(VIRT_KERNEL_AREA) != 0);
#else
    BUILD_BUG_ON(l3_table_offset(VIRT_KERNEL_AREA) != 0 ||
                 l2_table_offset(VIRT_KERNEL_AREA) == 0);
#endif
#endif
}

grant_entry_v1_t *arch_init_gnttab(int nr_grant_frames)
{
    struct gnttab_setup_table setup;
    unsigned long frames[nr_grant_frames];

    setup.dom = DOMID_SELF;
    setup.nr_frames = nr_grant_frames;
    set_xen_guest_handle(setup.frame_list, frames);

    HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
    return map_frames(frames, nr_grant_frames);
}

unsigned long alloc_virt_kernel(unsigned n_pages)
{
    unsigned long addr;

    addr = virt_kernel_area_end;
    virt_kernel_area_end += PAGE_SIZE * n_pages;
    ASSERT(virt_kernel_area_end <= VIRT_DEMAND_AREA);

    return addr;
}

unsigned long map_frame_virt(unsigned long mfn)
{
    unsigned long addr;

    addr = alloc_virt_kernel(1);
    if ( map_frame_rw(addr, mfn) )
        return 0;

    return addr;
}
