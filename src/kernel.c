/*
 * kernel.c — VoidShell kstuff kernel interface
 *
 * VoidShell uses the "kstuff" PS5 kernel exploit to:
 *   1. Escalate privilege (set ucred authid = 0x4801000000000013)
 *   2. Read/write kernel virtual memory directly
 *   3. Patch sysentvec to toggle extended syscall availability
 *      (used by sentinel to enable/disable homebrew-only syscalls
 *       when a commercial game is running)
 *
 * The exploit interface is exposed through a set of primitives
 * (kernel_getshort, kernel_setshort, kernel_copyout, etc.) that
 * are provided by libkstuff or the payload loader.  VoidShell
 * assumes these are already available in its process context.
 *
 * Firmware offsets for g_sysentvec and g_sysentvec_ps4 are selected
 * by init_kernel_offsets() based on the running firmware version
 * (obtained via kernel_get_fw_version()).
 *
 * BSD syscall stubs (nmount, unmount, etc.) are thin wrappers that
 * call the syscall() libc function with the appropriate syscall numbers
 * as defined in FreeBSD's syscall table.
 */

#include "kernel.h"
#include "logging.h"
#include "ps5_sdk.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ============================================================
 * Global kernel state
 * ============================================================ */
uint64_t g_sysentvec          = 0;
uint64_t g_sysentvec_ps4      = 0;
/* KERNEL_ADDRESS_DATA_BASE is defined by ps5-payload-sdk crt1.o (patch.c),
 * populated by the exploit loader before main() runs. */

/* Dynamically resolved notification send function */
void *f_sceNotificationSend = NULL;

/* ============================================================
 * FreeBSD syscall numbers used by VoidShell
 * (from FreeBSD 12 / PS5 syscall table)
 * ============================================================ */
#define SYS_nmount    378
#define SYS_unmount    22
#define SYS_mount      21

/* ============================================================
 * init_kernel_offsets  (addr 0x104790)
 * ============================================================
 *
 * Detects firmware version and selects the correct kernel addresses
 * for the sysentvec structures.  Also loads libSceNotification.sprx
 * and resolves sceNotificationSend via sceKernelDlsym.
 *
 * Supported firmware versions:
 *   3.00, 3.10          -> data base + 0xCA0CD8 / 0xCA0E50
 *   3.20, 3.21          -> data base + 0xCA0CD8 / 0xCA0E50
 *   4.00, 4.02          -> data base + 0xD11BB8 / 0xD11D30
 *   4.03, 4.50, 4.51    -> data base + 0xD11BB8 / 0xD11D30
 *   5.00, 5.02          -> data base + 0xE00BE8 / 0xE00D60
 *   5.10, 5.50          -> data base + 0xE00BE8 / 0xE00D60
 *   6.00, 6.02          -> data base + 0xE210A8 / 0xE21220
 *   6.50                -> data base + 0xE210A8 / 0xE21220
 *   7.00, 7.01          -> data base + 0xE21AB8 / 0xE21C30
 *   7.20, 7.40          -> data base + 0xE21B78 / 0xE21CF0
 *   7.60, 7.61          -> data base + 0xE21B78 / 0xE21CF0
 *   8.00, 8.20          -> data base + 0xE21CA8 / 0xE21E20
 *   8.40, 8.60          -> data base + 0xE21CA8 / 0xE21E20
 *   9.00                -> data base + 0xDBA648 / 0xDBA7C0
 *   9.05, 9.20          -> data base + 0xDBA648 / 0xDBA7C0
 *   9.40, 9.60          -> data base + 0xDBA648 / 0xDBA7C0
 *  10.00, 10.01         -> data base + 0xDBA6D8 / 0xDBA850
 *  10.20                -> data base + 0xDBA6D8 / 0xDBA850
 *  10.40, 10.60         -> data base + 0xDBA6D8 / 0xDBA850
 */
int init_kernel_offsets(void)
{
    uint32_t fw;
    uint32_t fw_maj; /* upper 16 bits only */
    int      handle;
    int      found = 0;

    fw     = kernel_get_fw_version();
    fw_maj = fw & 0xFFFF0000u;

    /* Load notification module */
    handle = sceKernelLoadStartModule(
                 "/system/common/lib/libSceNotification.sprx",
                 0, NULL, 0, NULL, NULL);
    if (handle > 0)
        sceKernelDlsym(handle, "sceNotificationSend", &f_sceNotificationSend);

    /* Select sysentvec offsets by firmware version */
    if      (fw_maj == 0x03000000 || fw_maj == 0x03100000 ||
             fw_maj == 0x03200000 || fw_maj == 0x03210000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xCA0CD8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xCA0E50;
        found = 1;
    }
    else if (fw_maj == 0x04000000 || fw_maj == 0x04020000 ||
             fw_maj == 0x04030000 || fw_maj == 0x04500000 ||
             fw_maj == 0x04510000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xD11BB8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xD11D30;
        found = 1;
    }
    else if (fw_maj == 0x05000000 || fw_maj == 0x05020000 ||
             fw_maj == 0x05100000 || fw_maj == 0x05500000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE00BE8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE00D60;
        found = 1;
    }
    else if (fw_maj == 0x06000000 || fw_maj == 0x06020000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE210A8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE21220;
        found = 1;
    }
    else if (fw_maj == 0x06500000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE210A8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE21220;
        found = 1;
    }
    else if (fw_maj == 0x07000000 || fw_maj == 0x07010000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE21AB8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE21C30;
        found = 1;
    }
    else if (fw_maj == 0x07200000 || fw_maj == 0x07400000 ||
             fw_maj == 0x07600000 || fw_maj == 0x07610000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE21B78;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE21CF0;
        found = 1;
    }
    else if (fw_maj == 0x08000000 || fw_maj == 0x08200000 ||
             fw_maj == 0x08400000 || fw_maj == 0x08600000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xE21CA8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xE21E20;
        found = 1;
    }
    else if (fw_maj == 0x09000000 || fw_maj == 0x09050000 ||
             fw_maj == 0x09200000 || fw_maj == 0x09400000 ||
             fw_maj == 0x09600000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xDBA648;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xDBA7C0;
        found = 1;
    }
    else if (fw_maj == 0x10000000 || fw_maj == 0x10010000 ||
             fw_maj == 0x10200000 || fw_maj == 0x10400000 ||
             fw_maj == 0x10600000) {
        g_sysentvec     = KERNEL_ADDRESS_DATA_BASE + 0xDBA6D8;
        g_sysentvec_ps4 = KERNEL_ADDRESS_DATA_BASE + 0xDBA850;
        found = 1;
    }

    if (found) {
        log_debug("INIT: Firmware 0x%x detected. Offsets loaded.", fw_maj);
    } else {
        log_debug("INIT: Unknown firmware version 0x%x. Kstuff toggle disabled.", fw_maj);
    }

    return found;
}

/* ============================================================
 * set_kstuff_state  (addr 0x104ba0)
 * ============================================================
 * Toggle kstuff by patching sysentvec+0x0E (syscall_count field).
 *
 * 0xDEB7 = kstuff ON  (enables PS5-native extended syscalls)
 * 0xFFFF = kstuff OFF (game-safe / PS4-compat mode)
 *
 * The patch is applied to both the PS5 and PS4-compat sysentvec structs
 * so that both contexts are in sync.
 */
void set_kstuff_state(int enable)
{
    uint16_t desired, current;

    if (g_sysentvec == 0)
        return;

    /* desired value: enable ? 0xDEB7 : 0xFFFF */
    desired = enable ? (uint16_t)KSTUFF_ON : (uint16_t)KSTUFF_OFF;

    current = kernel_getshort(g_sysentvec + 0x0E);
    if (current == desired)
        return; /* already in the desired state */

    kernel_setshort(g_sysentvec     + 0x0E, desired);
    kernel_setshort(g_sysentvec_ps4 + 0x0E, desired);

    log_debug("SENTINEL: Kstuff state set to %s (Val: 0x%x)",
              enable ? "ON" : "OFF", desired);
}

/* ============================================================
 * BSD syscall stubs
 * ============================================================
 *
 * These are syscall() wrappers for BSD system calls that are either
 * not in the PS5's libc stubs or need to be called directly.
 *
 * The syscall stub table (addresses 0x103810 – 0x104780) is a block
 * of ~200 identical one-liner wrappers generated by the C runtime
 * build environment.  We implement only the three used by VoidShell
 * (nmount, unmount, mount); the rest are declared for completeness
 * but the actual call sites all use only these.
 */

int nmount(void *iov, unsigned int niov, int flags)
{
    return (int)syscall(SYS_nmount, iov, niov, flags);
}

int unmount(const char *path, int flags)
{
    return (int)syscall(SYS_unmount, path, flags);
}

int mount(const char *type, const char *dir, int flags, void *data)
{
    return (int)syscall(SYS_mount, type, dir, flags, data);
}

/*
 * All other BSD syscall stubs from 0x103900–0x10478F are trivial wrappers
 * of the form:  return syscall(SYS_xxx, ...);
 * They are generated boilerplate and are not reconstructed individually.
 * The functions are declared as weak symbols so the linker will pick up
 * the real libc implementations if available.
 */
