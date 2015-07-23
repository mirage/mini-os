// ARM GIC implementation

#include <mini-os/os.h>
#include <mini-os/hypervisor.h>
#include <mini-os/console.h>
#include <libfdt.h>

//#define VGIC_DEBUG
#ifdef VGIC_DEBUG
#define DEBUG(_f, _a...) \
    printk("MINI_OS(file=vgic.c, line=%d) " _f , __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

extern void (*IRQ_handler)(void);

struct gic {
    volatile char *gicd_base;
    volatile char *gicc_base;
};

static struct gic gic;

// Distributor Interface
#define GICD_CTLR        0x0
#define GICD_ISENABLER    0x100
#define GICD_IPRIORITYR   0x400
#define GICD_ITARGETSR    0x800
#define GICD_ICFGR        0xC00

// CPU Interface
#define GICC_CTLR    0x0
#define GICC_PMR    0x4
#define GICC_IAR    0xc
#define GICC_EOIR    0x10
#define GICC_HPPIR    0x18

#define gicd(gic, offset) ((gic)->gicd_base + (offset))
#define gicc(gic, offset) ((gic)->gicc_base + (offset))

#define REG(addr) ((uint32_t *)(addr))

static inline uint32_t REG_READ32(volatile uint32_t *addr)
{
    uint32_t value;
    __asm__ __volatile__("ldr %0, [%1]":"=&r"(value):"r"(addr));
    rmb();
    return value;
}

static inline void REG_WRITE32(volatile uint32_t *addr, unsigned int value)
{
    __asm__ __volatile__("str %0, [%1]"::"r"(value), "r"(addr));
    wmb();
}

static void gic_set_priority(struct gic *gic, int irq_number, unsigned char priority)
{
    uint32_t value;
    uint32_t *addr = REG(gicd(gic, GICD_IPRIORITYR)) + (irq_number >> 2);
    value = REG_READ32(addr);
    value &= ~(0xff << (8 * (irq_number & 0x3))); // clear old priority
    value |= priority << (8 * (irq_number & 0x3)); // set new priority
    REG_WRITE32(addr, value);
}

static void gic_route_interrupt(struct gic *gic, int irq_number, unsigned char cpu_set)
{
    uint32_t value;
    uint32_t *addr = REG(gicd(gic, GICD_ITARGETSR)) + (irq_number >> 2);
    value = REG_READ32(addr);
    value &= ~(0xff << (8 * (irq_number & 0x3))); // clear old target
    value |= cpu_set << (8 * (irq_number & 0x3)); // set new target
    REG_WRITE32(addr, value);
}

/* When accessing the GIC registers, we can't use LDREX/STREX because it's not regular memory. */
static __inline__ void clear_bit_non_atomic(int nr, volatile void *base)
{
    volatile uint32_t *tmp = base;
    tmp[nr >> 5] &= (unsigned long)~(1 << (nr & 0x1f));
}

static __inline__ void set_bit_non_atomic(int nr, volatile void *base)
{
    volatile uint32_t *tmp = base;
    tmp[nr >> 5] |= (1 << (nr & 0x1f));
}

/* Note: not thread safe (but we only support one CPU for now anyway) */
static void gic_enable_interrupt(struct gic *gic, int irq_number,
        unsigned char cpu_set, unsigned char level_sensitive)
{
    int *set_enable_reg;
    void *cfg_reg;

    // set priority
    gic_set_priority(gic, irq_number, 0x0);

    // set target cpus for this interrupt
    gic_route_interrupt(gic, irq_number, cpu_set);

    // set level/edge triggered
    cfg_reg = (void *)gicd(gic, GICD_ICFGR);
    if (level_sensitive) {
        clear_bit_non_atomic((irq_number * 2) + 1, cfg_reg);
    } else {
        set_bit_non_atomic((irq_number * 2) + 1, cfg_reg);
    }

    wmb();

    // enable forwarding interrupt from distributor to cpu interface
    set_enable_reg = (int *)gicd(gic, GICD_ISENABLER);
    set_enable_reg[irq_number >> 5] = 1 << (irq_number & 0x1f);
    wmb();
}

static void gic_enable_interrupts(struct gic *gic)
{
    // Global enable forwarding interrupts from distributor to cpu interface
    REG_WRITE32(REG(gicd(gic, GICD_CTLR)), 0x00000001);

    // Global enable signalling of interrupt from the cpu interface
    REG_WRITE32(REG(gicc(gic, GICC_CTLR)), 0x00000001);
}

static void gic_disable_interrupts(struct gic *gic)
{
    // Global disable signalling of interrupt from the cpu interface
    REG_WRITE32(REG(gicc(gic, GICC_CTLR)), 0x00000000);

    // Global disable forwarding interrupts from distributor to cpu interface
    REG_WRITE32(REG(gicd(gic, GICD_CTLR)), 0x00000000);
}

static void gic_cpu_set_priority(struct gic *gic, char priority)
{
    REG_WRITE32(REG(gicc(gic, GICC_PMR)), priority & 0x000000FF);
}

static unsigned long gic_readiar(struct gic *gic) {
    return REG_READ32(REG(gicc(gic, GICC_IAR))) & 0x000003FF; // Interrupt ID
}

static void gic_eoir(struct gic *gic, uint32_t irq) {
    REG_WRITE32(REG(gicc(gic, GICC_EOIR)), irq & 0x000003FF);
}

//FIXME Get event_irq from dt
#define EVENTS_IRQ 31
#define VIRTUALTIMER_IRQ 27

static void gic_handler(void) {
    unsigned int irq = gic_readiar(&gic);

    DEBUG("IRQ received : %i\n", irq);
    switch(irq) {
    case EVENTS_IRQ:
        do_hypervisor_callback(NULL);
        break;
    case VIRTUALTIMER_IRQ:
        /* We need to get this event to wake us up from block_domain,
         * but we don't need to do anything special with it. */
        break;
    case 1022:
    case 1023:
        return;  /* Spurious interrupt */
    default:
        DEBUG("Unhandled irq\n");
        break;
    }

    DEBUG("EIRQ\n");

    gic_eoir(&gic, irq);
}

void gic_init(void) {
    gic.gicd_base = NULL;
    int node = 0;
    int depth = 0;
    for (;;)
    {
        node = fdt_next_node(device_tree, node, &depth);
        if (node <= 0 || depth < 0)
            break;

        if (fdt_getprop(device_tree, node, "interrupt-controller", NULL)) {
            int len = 0;

            if (fdt_node_check_compatible(device_tree, node, "arm,cortex-a15-gic") &&
                fdt_node_check_compatible(device_tree, node, "arm,cortex-a7-gic")) {
                printk("Skipping incompatible interrupt-controller node\n");
                continue;
            }

            const uint64_t *reg = fdt_getprop(device_tree, node, "reg", &len);

            /* We have two registers (GICC and GICD), each of which contains
             * two parts (an address and a size), each of which is a 64-bit
             * value (8 bytes), so we expect a length of 2 * 2 * 8 = 32.
             * If any extra values are passed in future, we ignore them. */
            if (reg == NULL || len < 32) {
                printk("Bad 'reg' property: %p %d\n", reg, len);
                continue;
            }

            gic.gicd_base = to_virt((long) fdt64_to_cpu(reg[0]));
            gic.gicc_base = to_virt((long) fdt64_to_cpu(reg[2]));
            printk("Found GIC: gicd_base = %p, gicc_base = %p\n", gic.gicd_base, gic.gicc_base);
            break;
        }
    }
    if (!gic.gicd_base) {
        printk("GIC not found!\n");
        BUG();
    }
    wmb();

    /* Note: we could mark this as "device" memory here, but Xen will have already
     * set it that way in the second stage translation table, so it's not necessary.
     * See "Overlaying the memory type attribute" in the Architecture Reference Manual.
     */

    IRQ_handler = gic_handler;

    gic_disable_interrupts(&gic);
    gic_cpu_set_priority(&gic, 0xff);

    /* Must call gic_enable_interrupts before enabling individual interrupts, otherwise our IRQ handler
     * gets called endlessly with spurious interrupts. */
    gic_enable_interrupts(&gic);

    gic_enable_interrupt(&gic, EVENTS_IRQ /* interrupt number */, 0x1 /*cpu_set*/, 1 /*level_sensitive*/);
    gic_enable_interrupt(&gic, VIRTUALTIMER_IRQ /* interrupt number */, 0x1 /*cpu_set*/, 1 /*level_sensitive*/);
}
