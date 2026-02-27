#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

/*
 * kernel.h — kstuff kernel interface for VoidShell
 *
 * This module wraps the kstuff PS5 kernel exploit interface.
 * It provides:
 *   - Firmware version detection and sysentvec offset tables
 *   - Kernel r/w primitives (getshort/setshort etc.)
 *   - Privilege escalation (set_ucred_authid)
 *   - Syscall wrappers for PS5/FreeBSD-specific calls not in libc
 *
 * The sysentvec approach: by patching the syscall_count field at
 * sysentvec+0x0E, kstuff can toggle whether the PS5 kernel allows
 * "extended" syscalls.  0xDEB7 = enabled (PS5 native mode),
 * 0xFFFF = disabled (game-compatible / PS4 compat mode).
 */

/* ----------------------------------------------------------
 * Firmware-versioned kernel offsets (populated by init_kernel_offsets)
 * ---------------------------------------------------------- */
extern uint64_t g_sysentvec;      /* VA of PS5 native sysentvec             */
extern uint64_t g_sysentvec_ps4;  /* VA of PS4-compat sysentvec             */
extern uint64_t KERNEL_ADDRESS_DATA_BASE;  /* kernel data segment base       */

/* ----------------------------------------------------------
 * Kstuff state values for sysentvec+0x0E
 * ---------------------------------------------------------- */
#define KSTUFF_ON   0xDEB7   /* native syscalls enabled (kstuff active)    */
#define KSTUFF_OFF  0xFFFF   /* kstuff disabled, game-compatible mode       */

/* ----------------------------------------------------------
 * Exported functions
 * ---------------------------------------------------------- */

/**
 * Detect firmware version and populate g_sysentvec / g_sysentvec_ps4.
 * Also loads libSceNotification.sprx and resolves sceNotificationSend.
 *
 * @return 1 on success, 0 if firmware version is unrecognised.
 */
int init_kernel_offsets(void);

/**
 * Set kstuff mode on both sysentvec structs.
 *
 * @param enable  Non-zero = enable kstuff (0xDEB7), zero = disable (0xFFFF).
 *
 * If g_sysentvec is 0 (not initialised), this is a no-op.
 * If the current value already matches the desired value, no write occurs.
 */
void set_kstuff_state(int enable);

/* ----------------------------------------------------------
 * Kernel memory access primitives (implemented via kstuff exploit)
 * ---------------------------------------------------------- */
uint16_t kernel_getshort(uint64_t kaddr);
void     kernel_setshort(uint64_t kaddr, uint16_t value);
uint32_t kernel_getdword(uint64_t kaddr);
void     kernel_setdword(uint64_t kaddr, uint32_t value);
int      kernel_copyout(uint64_t kaddr, void *buf, uint64_t len);
int      kernel_copyin(uint64_t kaddr, const void *buf, uint64_t len);
uint32_t kernel_get_fw_version(void);
int      kernel_set_ucred_authid(uint32_t uid, uint64_t authid);

/* ----------------------------------------------------------
 * PS5/FreeBSD syscall wrappers (generated from the syscall stubs)
 * These are thin wrappers around the syscall() libc function.
 * Only those actually used by VoidShell are listed here.
 * ---------------------------------------------------------- */
int  nmount(void *iov, unsigned int niov, int flags);
int  unmount(const char *path, int flags);
int  mount(const char *type, const char *dir, int flags, void *data);

/* BSD thread syscalls */
int  thr_create(void *ctx, long *id, int flags);
void thr_exit(long *state);
int  thr_self(long *id);
int  thr_kill(long id, int sig);
int  thr_new(void *param, int param_size);
int  thr_set_name(long id, const char *name);

#endif /* KERNEL_H */
