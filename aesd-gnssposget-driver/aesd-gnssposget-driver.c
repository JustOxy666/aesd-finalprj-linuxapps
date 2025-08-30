/*
* @file aesd-gnssposget-driver.c
* @brief A driver for the GNSS position getter
*
* Driver implements TTY Line Discipline that
* processes all incoming or outgoing characters
* from/to UART3 port of Raspberry Pi 4B that is bound to
* /dev/ttyAMA1 device (uBlox NEO 6M GNSS module)
*
*
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/tty_ldisc.h> /* struct tty_ldisc_ops */
#include <linux/slab.h>  /* kmalloc */
#include <linux/gfp.h>  /* kmalloc flags */
#include <linux/tty.h>
#include <linux/kfifo.h> /* kfifo circular buffer */
#include <linux/string.h>

#include "aesd-gnssposget-driver.h"

#define N_GNSSPOSGET 20
#define GNSSPOSGET_BUSY	1
#define GNSSPOSGET_ACTIVE	2

DEFINE_KFIFO(nmea_kfifo, struct nmea_cbuf, CIRC_BUFFER_SIZE);

MODULE_AUTHOR("JustOxy666");
MODULE_LICENSE("Dual BSD/GPL");

static const char nmea_prefix[] = "$XXXXX";
static const char gptxt_prefix[] = "$GPTXT";
static const char gprmc_prefix[] = "$GPRMC";
static const char gpgsv_prefix[] = "$GPGSV";
#define NMEA_LEN           (sizeof(nmea_prefix) - 1)

struct n_gnssposget *gnssposget_ldisc;

static int gnssposget_open(struct tty_struct *tty)
{
    struct n_gnssposget *n_gnssposget = tty->disc_data;
    struct nmea_cbuf nmea_kfifo;
	unsigned long flags;
    
	PDEBUG("%s() called (device=%s)\n", __func__, tty->name);
    PDEBUG("tty->magic=%d\n", tty->magic);

    /* Allocate memory for n_gnssposget struct */
    n_gnssposget = kzalloc(sizeof(struct n_gnssposget), GFP_KERNEL);
    if (!n_gnssposget) {
        PDEBUG("Failed to allocate memory for n_gnssposget\n");
        return -ENOMEM;
    }

    struct nmea_container *nmeatxt = kzalloc(sizeof(struct nmea_container), GFP_KERNEL);
    if (!nmeatxt) {
        PDEBUG("Failed to allocate memory for nmeatxt\n");
        kfree(n_gnssposget);
        return -ENOMEM;
    }

    /* Create circular buffer */
    
    n_gnssposget->nmea_cbuf = &nmea_kfifo;
    n_gnssposget->nmeatxt = nmeatxt;
    mutex_init(&n_gnssposget->mutex_lock);
    init_waitqueue_head(&n_gnssposget->read_queue);
    tty->disc_data = n_gnssposget;
    tty->receive_room = 128;

    tty_driver_flush_buffer(tty);

    spin_lock_irqsave(&n_gnssposget->lock, flags);
	set_bit(GNSSPOSGET_ACTIVE, &n_gnssposget->flags);
	spin_unlock_irqrestore(&n_gnssposget->lock, flags);
    
	return 0;
}

static void gnssposget_close(struct tty_struct *tty)
{
	struct n_gnssposget *n_gnssposget = tty->disc_data;
	unsigned long flags;

    mutex_destroy(&n_gnssposget->mutex_lock);
    kfree(n_gnssposget);

    spin_lock_irqsave(&n_gnssposget->lock, flags);
	clear_bit(GNSSPOSGET_ACTIVE, &n_gnssposget->flags);
	spin_unlock_irqrestore(&n_gnssposget->lock, flags);

	PDEBUG("%s() called (device=%s)\n", __func__, tty->name);
}

ssize_t	gnssposget_read(struct tty_struct *tty, struct file *file,
			U8 *buf, size_t nr,
            void **cookie, unsigned long offset)
{
    struct n_gnssposget *n_gnssposget = tty->disc_data;
    int ret = 0;

    if (!n_gnssposget)
       return -ENODEV;

    if (nr == 0) {
        PDEBUG("Omitting request to read %d bytes");
        return 0;
    }

    /* Check if kfifo has data */
    mutex_lock(&n_gnssposget->mutex_lock);
    if (kfifo_len(&nmea_kfifo) == 0U) {

        /* Block until data is available */
        mutex_unlock(&n_gnssposget->mutex_lock);
        if (wait_event_interruptible(n_gnssposget->read_queue,
                                    kfifo_len(&nmea_kfifo) > 0)) {
            ret = -ERESTARTSYS;
            goto unlock;
        }

        mutex_lock(&n_gnssposget->mutex_lock);
    }

    struct nmea_cbuf nmea;
    kfifo_get(&nmea_kfifo, &nmea);
    if (nr > nmea.len)
    {
        nr = nmea.len;
    }

	memcpy((U8*)buf, (U8*)nmea.buf, nr);
    ret = nr;

    unlock:
    mutex_unlock(&n_gnssposget->mutex_lock);
    return ret;
}

static void handle_nmea(struct n_gnssposget *n_gnssposget)
{
    /* Receive GPTXT, GPGSV & GPRMC only */
	PDEBUG("got NMEA: %s", n_gnssposget->nmeatxt->nmea_text);
	if (n_gnssposget->nmeatxt->index >= NMEA_LEN) {
	    if ((memcmp(n_gnssposget->nmeatxt->nmea_text, gptxt_prefix, NMEA_LEN) == 0) ||
            (memcmp(n_gnssposget->nmeatxt->nmea_text, gprmc_prefix, NMEA_LEN) == 0) ||
            (memcmp(n_gnssposget->nmeatxt->nmea_text, gpgsv_prefix, NMEA_LEN) == 0))
        {
            PDEBUG("valid message");
            struct nmea_cbuf nmea;
            if (n_gnssposget->nmeatxt->index > NMEA_MAX_LENGTH)
            {
                PDEBUG("WARNING!!! NMEA string is longer than receiving buffer!");
            }

            memcpy((U8*)nmea.buf, (U8*)n_gnssposget->nmeatxt->nmea_text, n_gnssposget->nmeatxt->index);
            nmea.len = n_gnssposget->nmeatxt->index;
            if (kfifo_put(&nmea_kfifo, nmea) == 0)
            {
                PDEBUG("kfifo full! Dropping oldest element");
                struct nmea_cbuf dummy;
                kfifo_get(&nmea_kfifo, &dummy);
                kfifo_put(&nmea_kfifo, nmea);
            }

            wake_up_interruptible(&n_gnssposget->read_queue);
        }
	}
}


static void gnssposget_receive(struct tty_struct *tty,
                           const U8 *cp,
                           const char *fp,
                           int count)
{
    struct n_gnssposget *n_gnssposget = tty->disc_data;
	unsigned long flags;
    int i = 0;

    if (!n_gnssposget) {
        PDEBUG("No GNSS position getter available\n");
        return;
    }

    if (!count) {
        PDEBUG("No data to process\n");
        return;
    }

    for (i = 0; i < count; i++) {
		U8 ch = cp[i];

        /* Start a new frame when we see '$' */
        spin_lock_irqsave(&n_gnssposget->lock, flags);
		if (ch == '$') {
			n_gnssposget->nmeatxt->valid_frame = true;
			n_gnssposget->nmeatxt->index = 0;
		}

        if (n_gnssposget->nmeatxt->valid_frame == false) {
			goto unlock;
        }

        /* Append byte with overflow guard */
		if (n_gnssposget->nmeatxt->index < (NMEA_MAX_LENGTH - 1)) {
            n_gnssposget->nmeatxt->nmea_text[n_gnssposget->nmeatxt->index++] = ch;
		} else {
			/* Too long; reset and wait for next '$' */
            PDEBUG("Incorrect NMEA, too long");
			n_gnssposget->nmeatxt->valid_frame = false;
			n_gnssposget->nmeatxt->index = 0;
			goto unlock;
		}

        /* End of line? NMEA lines end with \r\n (handle either) */
		if (ch == '\n' || ch == '\r') {
			handle_nmea(n_gnssposget);
			n_gnssposget->nmeatxt->valid_frame = false;
			n_gnssposget->nmeatxt->index = 0;
		}

        unlock:
	    spin_unlock_irqrestore(&n_gnssposget->lock, flags);
    }
}

static struct tty_ldisc_ops n_gnssposget_ldisc = {
    /* from above */
    .owner        = THIS_MODULE,
    .num          = N_GNSSPOSGET,
    .name         = "n_gnssposget",
    .read         = gnssposget_read,
    .open         = gnssposget_open,
    .close        = gnssposget_close,
    /* from below */
    .receive_buf  = gnssposget_receive,
};

/*----------------------------------------
     Init/cleanup kernel module
/*----------------------------------------*/
static int __init n_gnssposget_init(void)
{
    int result = 0;

    result = tty_register_ldisc(&n_gnssposget_ldisc);
    if (!result)
        pr_info("N_GNSSPOSGET line discipline registered\n");
    else
        pr_err("N_GNSSPOSGET: error registering line discipline: %d\n",
                result);

    return result;
}

static void __exit n_gnssposget_exit(void)
{
    tty_unregister_ldisc(&n_gnssposget_ldisc);
}


module_init(n_gnssposget_init);
module_exit(n_gnssposget_exit);
MODULE_ALIAS_LDISC(N_GNSSPOSGET);
