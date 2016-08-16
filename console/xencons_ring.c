#include <mini-os/types.h>
#include <mini-os/wait.h>
#include <mini-os/mm.h>
#include <mini-os/hypervisor.h>
#include <mini-os/events.h>
#include <mini-os/os.h>
#include <mini-os/lib.h>
#include <mini-os/xenbus.h>
#include <xen/io/console.h>
#include <xen/io/protocols.h>
#include <xen/io/ring.h>
#include <xen/hvm/params.h>
#include <mini-os/xmalloc.h>
#include <mini-os/gnttab.h>
#include "console.h"

DECLARE_WAIT_QUEUE_HEAD(console_queue);

static struct xencons_interface *console_ring;
uint32_t console_evtchn;

#ifdef CONFIG_PARAVIRT
void get_console(void *p)
{
    start_info_t *si = p;

    console_ring = mfn_to_virt(si->console.domU.mfn);
    console_evtchn = si->console.domU.evtchn;
}
#else
void get_console(void *p)
{
    uint64_t v = -1;

    hvm_get_parameter(HVM_PARAM_CONSOLE_EVTCHN, &v);
    console_evtchn = v;

    hvm_get_parameter(HVM_PARAM_CONSOLE_PFN, &v);
    console_ring = (struct xencons_interface *)map_frame_virt(v);
}
#endif

static inline void notify_daemon(struct consfront_dev *dev)
{
    /* Use evtchn: this is called early, before irq is set up. */
    if (!dev)
        notify_remote_via_evtchn(console_evtchn);
    else
        notify_remote_via_evtchn(dev->evtchn);
}

static inline struct xencons_interface *xencons_interface(void)
{
    return console_evtchn ? console_ring : NULL;
} 
 
int xencons_ring_send_no_notify(struct consfront_dev *dev, const char *data, unsigned len)
{	
    int sent = 0;
	struct xencons_interface *intf;
	XENCONS_RING_IDX cons, prod;

	if (!dev)
            intf = xencons_interface();
        else
            intf = dev->ring;
        if (!intf)
            return sent;

	cons = intf->out_cons;
	prod = intf->out_prod;
	mb();
	BUG_ON((prod - cons) > sizeof(intf->out));

	while ((sent < len) && ((prod - cons) < sizeof(intf->out)))
		intf->out[MASK_XENCONS_IDX(prod++, intf->out)] = data[sent++];

	wmb();
	intf->out_prod = prod;
    
    return sent;
}

int xencons_ring_send(struct consfront_dev *dev, const char *data, unsigned len)
{
    int sent;

    sent = xencons_ring_send_no_notify(dev, data, len);
    notify_daemon(dev);

    return sent;
}	



void console_handle_input(evtchn_port_t port, struct pt_regs *regs, void *data)
{
	struct consfront_dev *dev = (struct consfront_dev *) data;
#ifdef HAVE_LIBC
        int fd = dev ? dev->fd : -1;

        if (fd != -1)
            files[fd].read = 1;

        wake_up(&console_queue);
#else
	struct xencons_interface *intf = xencons_interface();
	XENCONS_RING_IDX cons, prod;

	cons = intf->in_cons;
	prod = intf->in_prod;
	mb();
	BUG_ON((prod - cons) > sizeof(intf->in));

	while (cons != prod) {
		xencons_rx(intf->in+MASK_XENCONS_IDX(cons,intf->in), 1, regs);
		cons++;
	}

	mb();
	intf->in_cons = cons;

	notify_daemon(dev);

	xencons_tx();
#endif
}

#ifdef HAVE_LIBC
int xencons_ring_avail(struct consfront_dev *dev)
{
	struct xencons_interface *intf;
	XENCONS_RING_IDX cons, prod;

        if (!dev)
            intf = xencons_interface();
        else
            intf = dev->ring;

	cons = intf->in_cons;
	prod = intf->in_prod;
	mb();
	BUG_ON((prod - cons) > sizeof(intf->in));

        return prod - cons;
}

int xencons_ring_recv(struct consfront_dev *dev, char *data, unsigned len)
{
	struct xencons_interface *intf;
	XENCONS_RING_IDX cons, prod;
        unsigned filled = 0;

        if (!dev)
            intf = xencons_interface();
        else
            intf = dev->ring;

	cons = intf->in_cons;
	prod = intf->in_prod;
	mb();
	BUG_ON((prod - cons) > sizeof(intf->in));

        while (filled < len && cons + filled != prod) {
                data[filled] = *(intf->in + MASK_XENCONS_IDX(cons + filled, intf->in));
                filled++;
	}

	mb();
        intf->in_cons = cons + filled;

	notify_daemon(dev);

        return filled;
}
#endif

struct consfront_dev *xencons_ring_init(void)
{
	int err;
	struct consfront_dev *dev;

	if (!console_evtchn)
		return 0;

	dev = malloc(sizeof(struct consfront_dev));
	memset(dev, 0, sizeof(struct consfront_dev));
	dev->nodename = "device/console";
	dev->dom = 0;
	dev->backend = 0;
	dev->ring_ref = 0;

#ifdef HAVE_LIBC
	dev->fd = -1;
#endif
	dev->evtchn = console_evtchn;
	dev->ring = xencons_interface();

	err = bind_evtchn(dev->evtchn, console_handle_input, dev);
	if (err <= 0) {
		printk("XEN console request chn bind failed %i\n", err);
                free(dev);
		return NULL;
	}
        unmask_evtchn(dev->evtchn);

	/* In case we have in-flight data after save/restore... */
	notify_daemon(dev);

	return dev;
}

void xencons_resume(void)
{
	(void)xencons_ring_init();
}

