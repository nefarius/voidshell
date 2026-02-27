/*
 * fan.c — PS5 fan speed control via /dev/icc_fan
 *
 * The ICC (Inter-Controller Communication) bus exposes the fan controller
 * via the character device /dev/icc_fan.  Speed is controlled by sending
 * ioctl 0xC01C8F07 with a 0x1C-byte struct whose first byte is the target
 * temperature in degrees Celsius.
 *
 * Both apply_fan_control() and the identical block inlined into load_config()
 * use the same ioctl path.
 */

#include "fan.h"
#include "types.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

/* ioctl opcode for PS5 ICC fan speed control */
#define ICC_FAN_IOCTL  0xC01C8F07u

/* Size of the ioctl data structure */
#define ICC_FAN_BUF_SIZE  0x1C

/* ============================================================
 * apply_fan_control  (addr 0x101260)
 * ============================================================ */
void apply_fan_control(void)
{
    int  fd;
    char buf[ICC_FAN_BUF_SIZE];

    if (!g_fan_enabled)
        return;

    fd = open("/dev/icc_fan", O_RDONLY);
    if (fd < 0)
        return;

    /* Zero the entire buffer, then set byte[0] = target temperature */
    memset(buf, 0, sizeof(buf));
    buf[0] = (char)g_fan_target_temp;

    ioctl(fd, ICC_FAN_IOCTL, buf);
    close(fd);
}
