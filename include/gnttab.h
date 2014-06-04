#ifndef __GNTTAB_H__
#define __GNTTAB_H__

#include <xen/grant_table.h>

#define NR_RESERVED_ENTRIES 8

/* NR_GRANT_FRAMES must be less than or equal to that configured in Xen */
#define NR_GRANT_FRAMES 4
#define NR_GRANT_ENTRIES (NR_GRANT_FRAMES * PAGE_SIZE / sizeof(grant_entry_t))

void init_gnttab(void);
grant_ref_t gnttab_alloc_and_grant(void **map);
grant_ref_t gnttab_grant_access(domid_t domid, unsigned long frame,
				int readonly);
grant_ref_t gnttab_grant_transfer(domid_t domid, unsigned long pfn);
unsigned long gnttab_end_transfer(grant_ref_t gref);
int gnttab_end_access(grant_ref_t ref);
const char *gnttabop_error(int16_t status);
void fini_gnttab(void);
void suspend_gnttab(void);
void resume_gnttab(void);
grant_entry_v1_t *arch_init_gnttab(int nr_grant_frames);
void arch_suspend_gnttab(grant_entry_v1_t *gnttab_table, int nr_grant_frames);
void arch_resume_gnttab(grant_entry_v1_t *gnttab_table, int nr_grant_frames);

#endif /* !__GNTTAB_H__ */
