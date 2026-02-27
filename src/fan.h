#ifndef FAN_H
#define FAN_H

/*
 * fan.h — PS5 fan speed control via /dev/icc_fan
 *
 * The PS5 exposes fan control through an ioctl on /dev/icc_fan.
 * The ioctl opcode is 0xC01C8F07 (PS5-specific ICC fan command).
 *
 * The ioctl data structure is 0x1C bytes:
 *   byte[0]   = target temperature in degrees Celsius
 *   remaining = zeroed
 */

/**
 * Apply fan control settings.
 *
 * If g_fan_enabled is false this is a no-op.
 * Opens /dev/icc_fan, sends ioctl 0xC01C8F07 with g_fan_target_temp
 * as the target temperature byte, then closes the fd.
 */
void apply_fan_control(void);

#endif /* FAN_H */
