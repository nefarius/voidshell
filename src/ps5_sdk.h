#ifndef PS5_SDK_H
#define PS5_SDK_H

/*
 * ps5_sdk.h — Forward declarations for PS5/FreeBSD SDK imports used by VoidShell.
 *
 * These symbols are either:
 *   (a) resolved from dynamically loaded .sprx modules via sceKernelDlsym, or
 *   (b) linked against libkernel / libc stubs that proxy the real kernel calls.
 *
 * All function signatures are derived from observed call sites in the decompiled
 * binary and from the official PS5 SDK documentation where available.
 *
 * Modules loaded at runtime (via sceSysmoduleLoadModule / sceKernelLoadStartModule):
 *   libkernel_sys.sprx          (base kernel)
 *   libSceSystemService.sprx    (system UI services)
 *   libSceUserService.sprx      (user account services)
 *   libSceAppInstUtil.sprx      (package installation)
 *   libSceNet.sprx              (network)
 *   libkernel_web.sprx          (web engine support)
 *   libSceSysmodule.sprx        (module loader)
 *   libSceLibcInternal.sprx     (libc)
 *   libSceFsInternalForVsh.sprx (filesystem access)
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * libkernel — basic kernel services
 * ============================================================ */

/** Sleep for the specified number of microseconds. */
int sceKernelUsleep(unsigned int usec);

/** Get the CPU temperature in degrees. */
int sceKernelGetCpuTemperature(int *temp_out);

/** Get the SoC sensor temperature. param0 = sensor index (0 = main). */
int sceKernelGetSocSensorTemperature(int sensor, int *temp_out);

/** Load and start a kernel module (SPRX). Returns module handle or error. */
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                              uint32_t flags, const void *opt, int *res);

/** Resolve a symbol from a loaded module handle. */
int sceKernelDlsym(int handle, const char *symbol, void **func_out);

/** Suspend the current process or system. */
int sceKernelSuspend(int flags);

/* ============================================================
 * libSceSystemService — system / shell UI
 * ============================================================ */

/** Launch an application by title ID. */
int sceSystemServiceLaunchApp(const char *title_id, const char **argv,
                               const void *launch_params);

/** Get the app-id (handle) of the currently running "big app" (foreground game). */
int sceSystemServiceGetAppIdOfRunningBigApp(void);

/** Get the title ID string from an app-id. */
int sceSystemServiceGetAppTitleId(int app_id, char *title_id_out, size_t size);

/* ============================================================
 * libSceUserService — user account
 * ============================================================ */

/** Get the user-id of the currently logged-in foreground user. */
int sceUserServiceGetForegroundUser(int *user_id_out);

/* ============================================================
 * libSceAppInstUtil — PKG installation
 * ============================================================ */

/** Initialise the AppInstUtil subsystem. Must be called before install APIs. */
int sceAppInstUtilInitialize(void);

/**
 * Install a PKG from a file path.
 * @param install_params  Pointer to a struct containing the pkg path.
 * @param progress_buf    Caller-allocated buffer for progress updates (0x18*n bytes).
 * @param info_buf        Caller-allocated info buffer (0x2700 bytes).
 */
int sceAppInstUtilInstallByPackage(const void *install_params,
                                    void *progress_buf,
                                    void *info_buf);

/** Terminate the AppInstUtil subsystem. */
int sceAppInstUtilTerminate(void);

/**
 * Register a title directory for the app installer.
 * @param title_id    Title ID string.
 * @param base_path   Base install path (e.g. "/user/app/").
 * @param flags       Reserved, pass 0.
 */
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *base_path,
                                      int flags);

/* ============================================================
 * libSceSysmodule — module management
 * ============================================================ */

/** Load a built-in PS5 sysmodule by ID. 0x80a = SYSMODULE_APP_INST_UTIL. */
int sceSysmoduleLoadModule(uint16_t module_id);

/** Unload a previously loaded sysmodule. */
int sceSysmoduleUnloadModule(uint16_t module_id);

/* ============================================================
 * libSceKernelNotification (loaded via dlsym from libSceNotification.sprx)
 * ============================================================ */

/**
 * Send a system notification popup.
 * @param type          Notification type (0 = normal toast).
 * @param buf           Pointer to notification buffer (0xC30 bytes, zeroed).
 * @param buf_size      Size of the buffer (0xC30).
 * @param flags         Reserved, pass 0.
 *
 * The message text is placed at buf+0x2D (offset inside the buffer struct).
 */
int sceKernelSendNotificationRequest(int type, void *buf, size_t buf_size, int flags);

/* ============================================================
 * kstuff kernel access primitives
 * (implemented in kernel.c, use kernel exploit for direct kernel r/w)
 * ============================================================ */

/** Read a 16-bit value from a kernel virtual address. */
uint16_t kernel_getshort(uint64_t kaddr);

/** Write a 16-bit value to a kernel virtual address. */
void     kernel_setshort(uint64_t kaddr, uint16_t value);

/** Read a 32-bit value from a kernel virtual address. */
uint32_t kernel_getdword(uint64_t kaddr);

/** Write a 32-bit value to a kernel virtual address. */
void     kernel_setdword(uint64_t kaddr, uint32_t value);

/**
 * Copy data out of kernel memory into user-space buffer.
 * @param kaddr   Kernel virtual address.
 * @param buf     User-space destination.
 * @param len     Number of bytes to copy.
 */
int kernel_copyout(uint64_t kaddr, void *buf, size_t len);

/**
 * Copy data from user-space buffer into kernel memory.
 * @param kaddr   Kernel virtual address.
 * @param buf     User-space source.
 * @param len     Number of bytes to copy.
 */
int kernel_copyin(uint64_t kaddr, const void *buf, size_t len);

/**
 * Resolve a symbol from a loaded kernel dynlib.
 * @param handle    Kernel dynlib handle.
 * @param name      Symbol name string.
 * @param sym_out   Receives the resolved address.
 */
int kernel_dynlib_dlsym(uint64_t handle, const char *name, void **sym_out);

/**
 * Get the currently running PS5 firmware version as a packed uint32.
 * e.g. 0x07600000 = FW 7.60
 */
uint32_t kernel_get_fw_version(void);

/**
 * Escalate privilege of the calling process.
 * Sets ucred authid to the supplied value.
 * Called as: kernel_set_ucred_authid(0xffffffff, 0x4801000000000013)
 */
int kernel_set_ucred_authid(uint32_t uid, uint64_t authid);

#ifdef __cplusplus
}
#endif

#endif /* PS5_SDK_H */
