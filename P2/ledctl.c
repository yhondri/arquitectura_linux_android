#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <linux/tty.h> /* For fg_console, MAX_NR_CONSOLES */
#include <linux/kd.h>  /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/errno.h>
#include <asm-generic/errno.h>
#include <asm-generic/errno-base.h>

/*
https://github.com/amitks/kbleds/blob/master/kbleds.c
kbleds_init
- ¿Cómo modificar drivers?
*/

struct tty_driver *leds_driver = NULL;

/*
 Permite establecer el valor de los leds. 
*/
static inline int set_leds(struct tty_driver *handler, unsigned int mask)
{
    // /usr/src/linux/drivers/char/vt_ioctl.c
    return (handler->ops->ioctl)(vc_cons[fg_console].d->port.tty, KDSETLED, mask);
}

struct tty_driver *get_kbd_driver_handler(void)
{
    return vc_cons[fg_console].d->port.tty->driver;
}

SYSCALL_DEFINE1(lin_ledctl, unsigned int, leds)
{

    if ((leds < 0) || (leds > 7))
    {
        printk(KERN_INFO "Error  valor inválido");
        return -EINVAL;
    }

    leds_driver = get_kbd_driver_handler();

    if (leds_driver == NULL)
    {
        /*No se ha encontrado el dispositivo.
        http://man7.org/linux/man-pages/man3/errno.3.html
        */
        return -ENODEV;
    }

    unsigned int mLeds = 0;

    // 0x1 es una mascara 00000001 si hacemos un or con eso pones el bit 1 a 1

    if (leds & 0x1)
    {
        mLeds |= 0x1;
    }

    if (leds & 0x4)
    {
        mLeds |= 0x2;
    }

    if (leds & 0x2)
    {
        mLeds |= 0x4;
    }

    return set_leds(leds_driver, mLeds);
}