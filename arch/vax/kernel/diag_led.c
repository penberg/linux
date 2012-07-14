#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/diag_led.h>
#include <asm/io.h>
#include <asm/mv.h>


/*
 * This driver is licensed under the terms of the GNU General Public
 * License Version 2 (GPLv2) or any later version.
 *
 * (C) 2004 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 */

#define DIAG_LED_DEBUG


MODULE_AUTHOR ("Jan-Benedict Glaw <jbglaw@lug-owl.de>");
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("Hackish driver for VAXens diagnostic LEDs");

static volatile uint8_t __iomem *diag;
static uint8_t state;
static int inverted;


/*
 * This function tries to find a base address. If you get a message
 * that your system isn't yet supported, add the correct address
 * right here.
 */
static unsigned long
diag_led_get_base (void)
{
	inverted = 0;

	if (is_ka46 ()) {
		inverted = 1;
		return DIAG_LED_KA46_BASE;
	} else if (is_ka42 ()) {
		inverted = 1;
		return DIAG_LED_KA42_BASE;
	} else if (is_ka48 ()) {
		inverted = 1;
		return DIAG_LED_KA48_BASE;
	} else if (is_ka49 ()) {
		inverted = 1;
		return DIAG_LED_KA49_BASE;
	} else if (is_ka52 ()) {
		inverted = 1;
		return DIAG_LED_KA52_BASE;
	} else if (is_vxt ()) {
		inverted = 1;
		return DIAG_LED_VXT_BASE;
#if 0
	} else if (is_ka670 ()) {
		inverted = 1;
		return DIAG_LED_KA670_BASE;
#endif
	} else if (is_ka43 ()) {
		inverted = 1;
		return DIAG_LED_KA43_BASE;
	} else {
		printk (KERN_ERR "diag_led: No base address known for your machine yet!\n");
		return 0;
	}
}

/*
 * A binary "1" for a lit LED, a binary "0" for an off LED
 */
int
diag_led_set_state (uint8_t new_state)
{
	if (!diag)
		return -ENODEV;

	if (inverted) {
		*diag = new_state ^ 0xff;
		state = new_state;
	} else {
		*diag = new_state;
		state = new_state;
	}

	return 0;
}

uint8_t
diag_led_get_state (void)
{
	if (!diag) {
		printk (KERN_ERR "Attention, there's no diag LEDs known on "
				"your system!!!\n");
		dump_stack ();
		return 0;
	}

	return state;
}

/*
 * led_num = 0 --> first LED
 * led_num = 1 --> second LED
 * led_num = 2 --> third LED
 * ...
 */
int
diag_led_on (int led_num)
{
	uint8_t new_state;

	if (led_num < 0 || led_num > 7) {
		printk (KERN_ERR "led_num out of range!\n");
		dump_stack ();
		return -EINVAL;
	}

	new_state = diag_led_get_state () | (1 << led_num);

	return diag_led_set_state (new_state);
}

/*
 * led_num = 0 --> first LED
 * led_num = 1 --> second LED
 * led_num = 2 --> third LED
 * ...
 */
int
diag_led_off (int led_num)
{
	uint8_t new_state;

	if (led_num < 0 || led_num > 7) {
		printk (KERN_ERR "led_num out of range!\n");
		dump_stack ();
		return -EINVAL;
	}

	new_state = diag_led_get_state () & ~(1 << led_num);

	return diag_led_set_state (new_state);
}

#ifdef DIAG_LED_DEBUG
static void
diag_led_knight_rider (void)
{
	int i, j;

	for (i = 0; i < 10; i++) {
		for (j = 0; j < 7; j++) {
			diag_led_set_state (1 << j);
			mdelay (30);
		}
		for (j = 7; j > 1; j--) {
			diag_led_set_state (1 << j);
			mdelay (30);
		}
	}

	return;
}
#endif /* DIAG_LED_DEBUG */

/*
 * Find memory base and map that address
 */
int __init
diag_led_probe (struct device *busdev)
{
	unsigned long base_address = diag_led_get_base ();

	if (!base_address)
		return -ENODEV;

	diag = ioremap (base_address, 1);
	if (!diag) {
		/* FIXME: Register with /proc/iomem */
		printk (KERN_ERR "Failed to ioremap (0x%08lx, 2)\n", base_address);
		return -ENOMEM;
	}

	printk (KERN_INFO "Using diagnostic LEDs at 0x%08lx (virt 0x%p)\n",
			base_address, diag);
#ifdef DIAG_LED_DEBUG
	diag_led_knight_rider ();
#endif /* DIAG_LED_DEBUG */

	diag_led_set_state (0x00);

	return 0;
}

/*
 * unmap the diag LEDs
 */
void __exit
diag_led_exit (void)
{
	if (diag) {
		printk (KERN_INFO "Shutting down diag LEDs at virt 0x%p\n",
				diag);
		iounmap ((void *) diag);
	}

	return;
}

static struct device_driver diag_led_driver = {
        .name   = "diag_led",
        .bus    = &platform_bus_type,
        .probe  = diag_led_probe,
};

static int __init
diag_led_init (void)
{
        return driver_register (&diag_led_driver);
}


EXPORT_SYMBOL (diag_led_set_state);
EXPORT_SYMBOL (diag_led_get_state);
EXPORT_SYMBOL (diag_led_on);
EXPORT_SYMBOL (diag_led_off);

module_init (diag_led_init);
module_exit (diag_led_exit);

