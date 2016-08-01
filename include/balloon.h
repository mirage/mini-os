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

#ifndef _BALLOON_H_
#define _BALLOON_H_

#ifdef CONFIG_BALLOON

extern unsigned long nr_max_pages;
extern unsigned long virt_kernel_area_end;

void get_max_pages(void);
void arch_remap_p2m(unsigned long max_pfn);
void mm_alloc_bitmap_remap(void);

#else /* CONFIG_BALLOON */

static inline void get_max_pages(void) { }
static inline void arch_remap_p2m(unsigned long max_pfn) { }
static inline void mm_alloc_bitmap_remap(void) { }

#endif /* CONFIG_BALLOON */
#endif /* _BALLOON_H_ */
