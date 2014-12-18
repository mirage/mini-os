#include <assert.h>
#include <console.h>
#include <kernel.h>

void __assert(const char *function,
              const char *file,
              int line,
              const char *assertion)
{
    printk("%s:%d: %s: Assertion %s failed\n", file, line, function, assertion);
    BUG();
}
