#ifndef AESD_GNSSPOSGET_DRIVER_H
#define AESD_GNSSPOSGET_DRIVER_H

#define __KERNEL__

#define NMEA_MAX_LENGTH 		(128)
#define CIRC_BUFFER_SIZE	 	(16)
#define MAGIC_NUMBER	 		(0x5101)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#undef PDEBUG             /* undef it, just in case */
#ifdef GNSSPOSGET_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "gnssposget: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

typedef unsigned char U8;

struct nmea_container {
    U8 nmea_text[NMEA_MAX_LENGTH];
	bool valid_frame;
    int index;
};

struct nmea_cbuf {
	U8 buf[NMEA_MAX_LENGTH];
	int len;
};

/**
 * struct n_gnssposget - per device instance data structure
 * @magic: magic value for structure
 * @tbusy: reentrancy flag for tx wakeup code
 * @woke_up: tx wakeup needs to be run again as it was called while @tbusy
 * @tx_buf_list: list of pending transmit frame buffers
 * @rx_buf_list: list of received frame buffers
 * @tx_free_buf_list: list unused transmit frame buffers
 * @rx_free_buf_list: list unused received frame buffers
 */
struct n_gnssposget {
	int						magic;
	// bool					tbusy;
	// bool					woke_up;
	struct nmea_container	*nmeatxt;
	struct nmea_cbuf		*nmea_cbuf;
	struct mutex			mutex_lock;
	wait_queue_head_t 		read_queue;
	spinlock_t 				lock;
	unsigned long 			flags;
	struct tty_struct		*tty_for_write_work;
};

#endif /* AESD_GNSSPOSGET_DRIVER_H */
