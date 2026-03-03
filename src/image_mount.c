/*
 * image_mount.c — VoidShell image auto-mount engine
 *
 * Ported from ShadowMountPlus (SMP) by VoidWhisper.
 * Supports .exfat (exfatfs), .ffpkg (UFS), and .ffpfs (PFS) images
 * via LVD (/dev/lvdctl) or MD (/dev/mdctl) ioctls + nmount().
 *
 * Mount tree: /data/imgmnt/{exfatmnt,ufsmnt,pfsmnt}/<basename>
 * Cache:      image_cache[MAX_IMAGE_MOUNTS]
 * Config:     [ImageMount] section in /data/voidshell/config.ini
 */

#include "image_mount.h"
#include "types.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <sys/mdioctl.h>
#include <stdbool.h>
#include <stdint.h>
#include "ps5_sdk.h"

/* ============================================================
 * LVD / MD constants (verbatim from ShadowMountPlus)
 * ============================================================ */
#define LVD_CTRL_PATH "/dev/lvdctl"
#define MD_CTRL_PATH  "/dev/mdctl"
#define SCE_LVD_IOC_ATTACH  0xC0286D00
#define SCE_LVD_IOC_DETACH  0xC0286D01

#define LVD_SECTOR_SIZE_EXFAT 512u
#define LVD_SECTOR_SIZE_UFS   4096u
#define LVD_SECTOR_SIZE_PFS   32768u

#define LVD_ATTACH_IO_VERSION             1
#define LVD_ATTACH_IMAGE_TYPE             0
#define LVD_ATTACH_IMAGE_TYPE_UFS         7   /* DownloadData/LWFS */
#define LVD_ATTACH_IMAGE_TYPE_PFS         0   /* also works: 5 */
#define LVD_ATTACH_LAYER_COUNT            1
#define LVD_ATTACH_LAYER_ARRAY_SIZE       3
#define LVD_ENTRY_TYPE_FILE               1
#define LVD_ENTRY_FLAG_NO_BITMAP          0x1
#define LVD_NODE_WAIT_US                  100000
#define LVD_NODE_WAIT_RETRIES             100

#define LVD_ATTACH_OPTION_FLAGS_DEFAULT   0x9   /* RO */
#define LVD_ATTACH_OPTION_FLAGS_RW        0x8   /* RW */
#define LVD_ATTACH_OPTION_NORM_DD_RO      0x1E  /* UFS RO */
#define LVD_ATTACH_OPTION_NORM_DD_RW      0x16  /* UFS RW */

/* nmount flags */
#define UFS_NMOUNT_FLAG_RW  0x10000000u
#define UFS_NMOUNT_FLAG_RO  0x10000001u

/* PFS mount options */
#define DEVPFS_BUDGET_GAME  "game"
#define PFS_MOUNT_MKEYMODE  "SD"
#define PFS_MOUNT_SIGVERIFY 0
#define PFS_MOUNT_PLAYGO    0
#define PFS_MOUNT_DISC      0
#define PFS_ZERO_EKPFS_KEY_HEX \
    "0000000000000000000000000000000000000000000000000000000000000000"

/* Mount subdirectory names */
#define IMAGE_MOUNT_SUBDIR_EXFAT "exfatmnt"
#define IMAGE_MOUNT_SUBDIR_UFS   "ufsmnt"
#define IMAGE_MOUNT_SUBDIR_PFS   "pfsmnt"

/* ============================================================
 * iovec helpers (verbatim from SMP)
 * ============================================================ */
#define IOVEC_ENTRY(x) \
    { (void *)(x), ((const char *)(x) == NULL \
        ? 0u : (size_t)(strlen((const char *)(x)) + 1u)) }
#define IOVEC_SIZE(x) (sizeof(x) / sizeof(struct iovec))

/* ============================================================
 * LVD ioctl structures (verbatim from SMP)
 * ============================================================ */
typedef struct {
    uint16_t source_type;
    uint8_t  entry_flags;
    uint8_t  reserved0;
    uint32_t reserved1;
    const char *path;
    uint64_t offset;
    uint64_t size;
    const char *bitmap_path;
    uint64_t bitmap_offset;
    uint64_t bitmap_size;
} lvd_kernel_layer_t;

typedef struct {
    uint32_t io_version;
    int32_t  device_id;
    uint32_t sector_size_0;
    uint32_t sector_size_1;
    uint16_t option_len;
    uint16_t image_type;
    uint32_t layer_count;
    uint64_t device_size;
    lvd_kernel_layer_t *layers_ptr;
} lvd_ioctl_attach_t;

typedef struct {
    uint32_t reserved0;
    int32_t  device_id;
    uint8_t  reserved[0x20];
} lvd_ioctl_detach_t;

/* ============================================================
 * Forward declarations of statics
 * ============================================================ */
static image_fs_type_e detect_image_fs_type(const char *name);
static void build_image_mount_point(const char *file_path,
                                    image_fs_type_e fs_type,
                                    char *mount_point, size_t sz);
static bool wait_for_dev_node(const char *devname, bool should_exist);
static uint16_t lvd_option_len_from_flags(uint16_t options);
static bool detach_lvd_unit(int unit_id);
static bool detach_md_unit(int unit_id);
static bool unmount_image(const char *file_path, int unit_id,
                          attach_backend_e backend);
static bool mount_image(const char *file_path, image_fs_type_e fs_type);
static void cache_image_mount(const char *path, const char *mount_point,
                              int unit_id, attach_backend_e backend);
static bool is_path_mountpoint(const char *path);

/* ============================================================
 * Helper: detect FS type from file extension
 * ============================================================ */
static image_fs_type_e detect_image_fs_type(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return IMAGE_FS_UNKNOWN;
    if (strcasecmp(dot, ".ffpkg") == 0) return IMAGE_FS_UFS;
    if (strcasecmp(dot, ".exfat") == 0) return IMAGE_FS_EXFAT;
    if (strcasecmp(dot, ".ffpfs") == 0) return IMAGE_FS_PFS;
    return IMAGE_FS_UNKNOWN;
}

static const char *image_fs_subdir(image_fs_type_e fs_type)
{
    if (fs_type == IMAGE_FS_UFS)   return IMAGE_MOUNT_SUBDIR_UFS;
    if (fs_type == IMAGE_FS_EXFAT) return IMAGE_MOUNT_SUBDIR_EXFAT;
    if (fs_type == IMAGE_FS_PFS)   return IMAGE_MOUNT_SUBDIR_PFS;
    return "unknown";
}

static const char *image_fs_name(image_fs_type_e fs_type)
{
    if (fs_type == IMAGE_FS_UFS)   return "ufs";
    if (fs_type == IMAGE_FS_EXFAT) return "exfatfs";
    if (fs_type == IMAGE_FS_PFS)   return "pfs";
    return "unknown";
}

static const char *backend_name_str(attach_backend_e b)
{
    if (b == ATTACH_BACKEND_LVD) return "LVD";
    if (b == ATTACH_BACKEND_MD)  return "MD";
    return "UNKNOWN";
}

/* ============================================================
 * Helper: strip extension, return basename without ext
 * ============================================================ */
static void strip_extension(const char *filename, char *out, size_t out_size)
{
    const char *dot = strrchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, filename, len);
    out[len] = '\0';
}

/* ============================================================
 * Helper: build mount point path
 * ============================================================ */
static void build_image_mount_point(const char *file_path,
                                    image_fs_type_e fs_type,
                                    char *mount_point, size_t sz)
{
    const char *filename = strrchr(file_path, '/');
    filename = filename ? filename + 1 : file_path;
    char mount_name[512];
    strip_extension(filename, mount_name, sizeof(mount_name));
    snprintf(mount_point, sz, "%s/%s/%s",
             IMAGE_MOUNT_BASE, image_fs_subdir(fs_type), mount_name);
}

/* ============================================================
 * Helper: wait for /dev/lvdN or /dev/mdN to appear/disappear
 * ============================================================ */
static bool wait_for_dev_node(const char *devname, bool should_exist)
{
    for (int i = 0; i < LVD_NODE_WAIT_RETRIES; i++) {
        if ((access(devname, F_OK) == 0) == should_exist)
            return true;
        sceKernelUsleep(LVD_NODE_WAIT_US);
    }
    return false;
}

/* ============================================================
 * Helper: derive option_len from raw LVD flags
 * (exact mirror of sceFsLvdAttachCommon derivation in SMP)
 * ============================================================ */
static uint16_t lvd_option_len_from_flags(uint16_t options)
{
    if ((options & 0x800Eu) != 0u) {
        uint32_t raw = (uint32_t)options;
        uint32_t len = (raw & 0xFFFF8000u) + ((raw & 2u) << 6)
                     + (8u * (raw & 1u)) + (2u * ((raw >> 2) & 1u))
                     + (2u * (raw & 8u)) + 4u;
        return (uint16_t)len;
    }
    return (uint16_t)(8u * ((uint32_t)options & 1u) + 4u);
}

/* ============================================================
 * Helper: LVD attach option flags
 * ============================================================ */
static uint16_t get_lvd_attach_option(image_fs_type_e fs_type, bool ro)
{
    if (fs_type == IMAGE_FS_UFS)
        return ro ? LVD_ATTACH_OPTION_NORM_DD_RO : LVD_ATTACH_OPTION_NORM_DD_RW;
    return ro ? LVD_ATTACH_OPTION_FLAGS_DEFAULT : LVD_ATTACH_OPTION_FLAGS_RW;
}

static uint32_t get_lvd_sector_size(image_fs_type_e fs_type)
{
    if (fs_type == IMAGE_FS_UFS)   return LVD_SECTOR_SIZE_UFS;
    if (fs_type == IMAGE_FS_PFS)   return LVD_SECTOR_SIZE_PFS;
    return LVD_SECTOR_SIZE_EXFAT;
}

static unsigned int get_md_attach_options(bool ro)
{
    unsigned int opts = MD_AUTOUNIT | MD_ASYNC;
    if (ro) opts |= MD_READONLY;
    return opts;
}

/* ============================================================
 * Helper: check if path is an active mount point
 * ============================================================ */
static bool is_path_mountpoint(const char *path)
{
    struct stat st_path, st_parent;
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s/..", path);
    if (stat(path, &st_path) != 0 || stat(parent, &st_parent) != 0)
        return false;
    return st_path.st_dev != st_parent.st_dev;
}

/* ============================================================
 * cache_image_mount — store or update a cache entry
 * ============================================================ */
static void cache_image_mount(const char *path, const char *mount_point,
                              int unit_id, attach_backend_e backend)
{
    /* Update if already present */
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (image_cache[k].valid &&
            strcmp(image_cache[k].path, path) == 0) {
            strncpy(image_cache[k].mount_point, mount_point, 1023);
            image_cache[k].unit_id = unit_id;
            image_cache[k].backend = backend;
            return;
        }
    }
    /* Find empty slot */
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (!image_cache[k].valid) {
            strncpy(image_cache[k].path,        path,        1023);
            strncpy(image_cache[k].mount_point, mount_point, 1023);
            image_cache[k].unit_id = unit_id;
            image_cache[k].backend = backend;
            image_cache[k].valid   = true;
            return;
        }
    }
    log_debug("  [IMG] image_cache full — cannot cache %s", path);
}

/* ============================================================
 * detach_lvd_unit / detach_md_unit
 * ============================================================ */
static bool detach_lvd_unit(int unit_id)
{
    if (unit_id < 0) return true;
    int fd = open(LVD_CTRL_PATH, O_RDWR);
    if (fd < 0) {
        log_debug("  [IMG][LVD] open %s for detach failed: %s",
                  LVD_CTRL_PATH, strerror(errno));
        return false;
    }
    lvd_ioctl_detach_t req;
    memset(&req, 0, sizeof(req));
    req.device_id = unit_id;
    bool ok = (ioctl(fd, SCE_LVD_IOC_DETACH, &req) == 0);
    if (!ok)
        log_debug("  [IMG][LVD] detach unit %d failed: %s",
                  unit_id, strerror(errno));
    close(fd);
    char devname[64];
    snprintf(devname, sizeof(devname), "/dev/lvd%d", unit_id);
    if (!wait_for_dev_node(devname, false))
        log_debug("  [IMG][LVD] device node still present after detach: %s",
                  devname);
    return ok;
}

static bool detach_md_unit(int unit_id)
{
    if (unit_id < 0) return true;
    int fd = open(MD_CTRL_PATH, O_RDWR);
    if (fd < 0) {
        log_debug("  [IMG][MD] open %s for detach failed: %s",
                  MD_CTRL_PATH, strerror(errno));
        return false;
    }
    struct md_ioctl req;
    memset(&req, 0, sizeof(req));
    req.md_version = MDIOVERSION;
    req.md_unit    = (unsigned int)unit_id;
    bool ok = true;
    if (ioctl(fd, MDIOCDETACH, &req) != 0) {
        int err = errno;
        req.md_options = MD_FORCE;
        if (ioctl(fd, MDIOCDETACH, &req) != 0) {
            log_debug("  [IMG][MD] detach unit %d failed: %s",
                      unit_id, strerror(errno));
            ok = false;
        } else {
            log_debug("  [IMG][MD] detach unit %d forced after error: %s",
                      unit_id, strerror(err));
        }
    }
    close(fd);
    char devname[64];
    snprintf(devname, sizeof(devname), "/dev/md%d", unit_id);
    if (!wait_for_dev_node(devname, false))
        log_debug("  [IMG][MD] device node still present after detach: %s",
                  devname);
    return ok;
}

/* ============================================================
 * unmount_image — unmount + detach a mounted image
 * ============================================================ */
static bool unmount_image(const char *file_path, int unit_id,
                          attach_backend_e backend)
{
    char mount_point[1024];
    image_fs_type_e fs_type = detect_image_fs_type(file_path);
    build_image_mount_point(file_path, fs_type, mount_point,
                            sizeof(mount_point));

    /* Unmount (may be stacked unionfs layers) */
    for (int i = 0; i < 4; i++) {
        if (!is_path_mountpoint(mount_point)) break;
        if (unmount(mount_point, 0) == 0)    continue;
        if (errno == ENOENT || errno == EINVAL) break;
        if (unmount(mount_point, MNT_FORCE) != 0
            && errno != ENOENT && errno != EINVAL) {
            log_debug("  [IMG][%s] unmount failed for %s: %s",
                      backend_name_str(backend), mount_point, strerror(errno));
            return false;
        }
    }

    if (is_path_mountpoint(mount_point)) {
        log_debug("  [IMG][%s] unmount incomplete for %s",
                  backend_name_str(backend), mount_point);
        return false;
    }

    bool detach_ok = true;
    if (backend != ATTACH_BACKEND_NONE && unit_id >= 0) {
        if (backend == ATTACH_BACKEND_MD)
            detach_ok = detach_md_unit(unit_id);
        else
            detach_ok = detach_lvd_unit(unit_id);
    }

    if (rmdir(mount_point) != 0 && errno != ENOENT && errno != ENOTEMPTY
        && errno != EBUSY)
        log_debug("  [IMG] failed to remove mount dir %s: %s",
                  mount_point, strerror(errno));

    return detach_ok;
}

/* ============================================================
 * mount_image — attach + nmount a single image file
 * ============================================================ */
static bool mount_image(const char *file_path, image_fs_type_e fs_type)
{
    bool mount_read_only = g_img_mount_ro;

    /* Skip if already cached */
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (image_cache[k].valid &&
            strcmp(image_cache[k].path, file_path) == 0)
            return true;
    }

    char mount_point[1024];
    build_image_mount_point(file_path, fs_type, mount_point,
                            sizeof(mount_point));

    /* Check if already mounted (non-empty dir is a live mount) */
    struct stat mst;
    if (stat(mount_point, &mst) == 0 && S_ISDIR(mst.st_mode)) {
        DIR *check = opendir(mount_point);
        if (check) {
            int count = 0;
            struct dirent *ce;
            while ((ce = readdir(check)) != NULL) {
                if (ce->d_name[0] != '.') { count++; break; }
            }
            closedir(check);
            if (count > 0) {
                log_debug("  [IMG] Already mounted (non-empty): %s",
                          mount_point);
                cache_image_mount(file_path, mount_point,
                                  -1, ATTACH_BACKEND_NONE);
                return true;
            }
        }
    }

    struct stat st;
    if (stat(file_path, &st) != 0) {
        log_debug("  [IMG] stat failed for %s: %s", file_path,
                  strerror(errno));
        return false;
    }
    if (st.st_size <= 0) {
        log_debug("  [IMG] invalid file size for %s", file_path);
        return false;
    }

    log_debug("  [IMG] Mounting image (%s): %s -> %s",
              image_fs_name(fs_type), file_path, mount_point);

    /* Create mount directories */
    char fs_root[1024];
    snprintf(fs_root, sizeof(fs_root), "%s/%s",
             IMAGE_MOUNT_BASE, image_fs_subdir(fs_type));
    mkdir(IMAGE_MOUNT_BASE, 0777);
    mkdir(fs_root, 0777);
    mkdir(mount_point, 0777);

    /* Choose backend based on config */
    attach_backend_e attach_backend = ATTACH_BACKEND_LVD;
    if (strcasecmp(g_img_backend, "md") == 0)
        attach_backend = ATTACH_BACKEND_MD;

    int  ret     = -1;
    int  unit_id = -1;
    char devname[64];
    memset(devname, 0, sizeof(devname));

    /* --- MD attach --- */
    if (attach_backend == ATTACH_BACKEND_MD) {
        int md_fd = open(MD_CTRL_PATH, O_RDWR);
        if (md_fd < 0) {
            log_debug("  [IMG][MD] open %s failed: %s",
                      MD_CTRL_PATH, strerror(errno));
            return false;
        }
        struct md_ioctl req;
        memset(&req, 0, sizeof(req));
        req.md_version    = MDIOVERSION;
        req.md_type       = MD_VNODE;
        req.md_file       = (char *)file_path;
        req.md_mediasize  = st.st_size;
        req.md_sectorsize = (fs_type == IMAGE_FS_UFS) ? 512u : 512u;
        req.md_options    = get_md_attach_options(mount_read_only);

        ret = ioctl(md_fd, MDIOCATTACH, &req);
        int last_err = errno;
        close(md_fd);

        if (ret != 0) {
            log_debug("  [IMG][MD] attach failed: %s", strerror(last_err));
            return false;
        }
        unit_id = (int)req.md_unit;
        if (unit_id < 0) {
            log_debug("  [IMG][MD] attach returned invalid unit: %d",
                      unit_id);
            return false;
        }
        snprintf(devname, sizeof(devname), "/dev/md%d", unit_id);
        if (!wait_for_dev_node(devname, true)) {
            log_debug("  [IMG][MD] device node did not appear: %s", devname);
            detach_md_unit(unit_id);
            return false;
        }
        log_debug("  [IMG][MD] attach unit=%d dev=%s", unit_id, devname);

    } else {
        /* --- LVD attach --- */
        int lvd_fd = open(LVD_CTRL_PATH, O_RDWR);
        if (lvd_fd < 0) {
            log_debug("  [IMG][LVD] open %s failed: %s",
                      LVD_CTRL_PATH, strerror(errno));
            return false;
        }

        lvd_kernel_layer_t *layers =
            calloc(LVD_ATTACH_LAYER_ARRAY_SIZE, sizeof(*layers));
        if (!layers) {
            log_debug("  [IMG] calloc layers failed");
            close(lvd_fd);
            return false;
        }
        layers[0].source_type  = LVD_ENTRY_TYPE_FILE;
        layers[0].entry_flags  = LVD_ENTRY_FLAG_NO_BITMAP;
        layers[0].path         = file_path;
        layers[0].offset       = 0;
        layers[0].size         = (uint64_t)st.st_size;

        lvd_ioctl_attach_t req;
        memset(&req, 0, sizeof(req));
        req.io_version    = LVD_ATTACH_IO_VERSION;
        req.image_type    = (fs_type == IMAGE_FS_UFS) ? LVD_ATTACH_IMAGE_TYPE_UFS
                          : (fs_type == IMAGE_FS_PFS) ? LVD_ATTACH_IMAGE_TYPE_PFS
                          : LVD_ATTACH_IMAGE_TYPE;
        req.layer_count   = LVD_ATTACH_LAYER_COUNT;
        req.device_size   = (uint64_t)st.st_size;
        req.layers_ptr    = layers;
        req.sector_size_0 = get_lvd_sector_size(fs_type);
        req.sector_size_1 = req.sector_size_0;
        req.device_id     = -1;

        uint16_t attach_opt = get_lvd_attach_option(fs_type, mount_read_only);
        if (fs_type == IMAGE_FS_UFS)
            req.option_len = attach_opt; /* UFS: pass normalized option directly */
        else
            req.option_len = lvd_option_len_from_flags(attach_opt);

        ret = ioctl(lvd_fd, SCE_LVD_IOC_ATTACH, &req);
        int last_err = errno;
        unit_id = req.device_id;
        close(lvd_fd);
        free(layers);

        if (ret != 0) {
            log_debug("  [IMG][LVD] attach failed: %s", strerror(last_err));
            return false;
        }
        if (unit_id < 0) {
            log_debug("  [IMG][LVD] attach returned invalid unit: %d",
                      unit_id);
            return false;
        }
        snprintf(devname, sizeof(devname), "/dev/lvd%d", unit_id);
        if (!wait_for_dev_node(devname, true)) {
            log_debug("  [IMG][LVD] device node did not appear: %s", devname);
            detach_lvd_unit(unit_id);
            return false;
        }
        log_debug("  [IMG][LVD] attach unit=%d dev=%s", unit_id, devname);
    }

    /* --- nmount filesystem --- */
    char mount_errmsg[256];
    memset(mount_errmsg, 0, sizeof(mount_errmsg));
    const char *sigverify = PFS_MOUNT_SIGVERIFY ? "1" : "0";
    const char *playgo    = PFS_MOUNT_PLAYGO    ? "1" : "0";
    const char *disc      = PFS_MOUNT_DISC      ? "1" : "0";
    const char *ekpfs_key = PFS_ZERO_EKPFS_KEY_HEX;

    struct iovec iov_ufs[] = {
        IOVEC_ENTRY("fstype"),     IOVEC_ENTRY("ufs"),
        IOVEC_ENTRY("from"),       IOVEC_ENTRY(devname),
        IOVEC_ENTRY("fspath"),     IOVEC_ENTRY(mount_point),
        IOVEC_ENTRY("budgetid"),   IOVEC_ENTRY(DEVPFS_BUDGET_GAME),
        IOVEC_ENTRY("async"),      IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("noatime"),    IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("automounted"),IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("errmsg"),     { (void *)mount_errmsg, sizeof(mount_errmsg) },
        IOVEC_ENTRY("force"),      IOVEC_ENTRY(NULL) };

    struct iovec iov_exfat[] = {
        IOVEC_ENTRY("from"),       IOVEC_ENTRY(devname),
        IOVEC_ENTRY("fspath"),     IOVEC_ENTRY(mount_point),
        IOVEC_ENTRY("fstype"),     IOVEC_ENTRY("exfatfs"),
        IOVEC_ENTRY("budgetid"),   IOVEC_ENTRY(DEVPFS_BUDGET_GAME),
        IOVEC_ENTRY("large"),      IOVEC_ENTRY("yes"),
        IOVEC_ENTRY("timezone"),   IOVEC_ENTRY("static"),
        IOVEC_ENTRY("async"),      IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("noatime"),    IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("ignoreacl"),  IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("automounted"),IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("errmsg"),     { (void *)mount_errmsg, sizeof(mount_errmsg) },
        IOVEC_ENTRY("force"),      IOVEC_ENTRY(NULL) };

    struct iovec iov_pfs[] = {
        IOVEC_ENTRY("from"),       IOVEC_ENTRY(devname),
        IOVEC_ENTRY("fspath"),     IOVEC_ENTRY(mount_point),
        IOVEC_ENTRY("fstype"),     IOVEC_ENTRY("pfs"),
        IOVEC_ENTRY("sigverify"),  IOVEC_ENTRY(sigverify),
        IOVEC_ENTRY("mkeymode"),   IOVEC_ENTRY(PFS_MOUNT_MKEYMODE),
        IOVEC_ENTRY("budgetid"),   IOVEC_ENTRY(DEVPFS_BUDGET_GAME),
        IOVEC_ENTRY("playgo"),     IOVEC_ENTRY(playgo),
        IOVEC_ENTRY("disc"),       IOVEC_ENTRY(disc),
        IOVEC_ENTRY("ekpfs"),      IOVEC_ENTRY(ekpfs_key),
        IOVEC_ENTRY("async"),      IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("noatime"),    IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("automounted"),IOVEC_ENTRY(NULL),
        IOVEC_ENTRY("errmsg"),     { (void *)mount_errmsg, sizeof(mount_errmsg) },
        IOVEC_ENTRY("force"),      IOVEC_ENTRY(NULL) };

    struct iovec *iov;
    unsigned int  iovlen;
    unsigned int  mount_flags;

    if (fs_type == IMAGE_FS_UFS) {
        iov       = iov_ufs;
        iovlen    = (unsigned int)IOVEC_SIZE(iov_ufs);
        mount_flags = mount_read_only ? UFS_NMOUNT_FLAG_RO : UFS_NMOUNT_FLAG_RW;
    } else if (fs_type == IMAGE_FS_EXFAT) {
        iov       = iov_exfat;
        iovlen    = (unsigned int)IOVEC_SIZE(iov_exfat);
        mount_flags = mount_read_only ? MNT_RDONLY : 0;
    } else if (fs_type == IMAGE_FS_PFS) {
        iov       = iov_pfs;
        iovlen    = (unsigned int)IOVEC_SIZE(iov_pfs);
        mount_flags = mount_read_only ? MNT_RDONLY : 0;
    } else {
        log_debug("  [IMG] unsupported fs type for %s", file_path);
        if (attach_backend == ATTACH_BACKEND_MD) detach_md_unit(unit_id);
        else                                     detach_lvd_unit(unit_id);
        return false;
    }

    ret = nmount(iov, iovlen, (int)mount_flags);
    if (ret != 0) {
        int mount_err = errno;
        if (mount_errmsg[0] != '\0')
            log_debug("  [IMG][%s] nmount errmsg: %s",
                      backend_name_str(attach_backend), mount_errmsg);
        log_debug("  [IMG][%s] nmount failed: %s",
                  backend_name_str(attach_backend), strerror(mount_err));
        if (attach_backend == ATTACH_BACKEND_MD) detach_md_unit(unit_id);
        else                                     detach_lvd_unit(unit_id);
        return false;
    }

    log_debug("  [IMG][%s] Mounted (%s) %s -> %s",
              backend_name_str(attach_backend), image_fs_name(fs_type),
              devname, mount_point);

    cache_image_mount(file_path, mount_point, unit_id, attach_backend);
    return true;
}

/* ============================================================
 * image_mount_init — zero cache, ensure base directories exist
 * ============================================================ */
void image_mount_init(void)
{
    memset(image_cache, 0, sizeof(image_cache));
    mkdir(IMAGE_MOUNT_BASE, 0777);
    mkdir(IMAGE_MOUNT_BASE "/" IMAGE_MOUNT_SUBDIR_EXFAT, 0777);
    mkdir(IMAGE_MOUNT_BASE "/" IMAGE_MOUNT_SUBDIR_UFS,   0777);
    mkdir(IMAGE_MOUNT_BASE "/" IMAGE_MOUNT_SUBDIR_PFS,   0777);
    log_debug("  [IMG] image_mount_init: cache zeroed, base dirs ensured");
}

/* ============================================================
 * image_mount_scan — scan configured paths for image files
 * ============================================================ */
void image_mount_scan(void)
{
    /* Prune cache entries whose source file is gone */
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (!image_cache[k].valid) continue;
        if (access(image_cache[k].path, F_OK) != 0) {
            log_debug("  [IMG] Source removed, unmounting: %s",
                      image_cache[k].path);
            if (unmount_image(image_cache[k].path, image_cache[k].unit_id,
                              image_cache[k].backend))
                image_cache[k].valid = false;
        }
    }

    /* Scan each configured path */
    for (int pi = 0; pi < g_img_scan_paths_count; pi++) {
        const char *scan_dir = g_img_scan_paths[pi];
        DIR *d = opendir(scan_dir);
        if (!d) continue;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     scan_dir, entry->d_name);

            image_fs_type_e fs_type = detect_image_fs_type(entry->d_name);
            if (fs_type == IMAGE_FS_UNKNOWN) continue;

            /* Stability check: file must not have been modified recently */
            struct stat st;
            if (stat(full_path, &st) != 0) continue;
            time_t now = time(NULL);
            if ((now - st.st_mtime) < g_img_stability_wait) {
                log_debug("  [IMG] %s not yet stable, skipping",
                          entry->d_name);
                continue;
            }

            mount_image(full_path, fs_type);
        }
        closedir(d);
    }
}

/* ============================================================
 * image_mount_shutdown — unmount all cached images
 * ============================================================ */
void image_mount_shutdown(void)
{
    log_debug("  [IMG] shutdown: unmounting all images");
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (!image_cache[k].valid) continue;
        (void)unmount_image(image_cache[k].path, image_cache[k].unit_id,
                            image_cache[k].backend);
        image_cache[k].valid = false;
    }
}

/* ============================================================
 * image_mount_list_json — serialize image_cache[] to JSON array
 * ============================================================ */
int image_mount_list_json(char *buf, int bufsz)
{
    int off = 0;
    off += snprintf(buf + off, bufsz - off, "[");
    bool first = true;
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (!image_cache[k].valid) continue;
        off += snprintf(buf + off, bufsz - off,
            "%s{\"path\":\"%s\",\"mount_point\":\"%s\","
            "\"backend\":\"%s\",\"unit\":%d}",
            first ? "" : ",",
            image_cache[k].path,
            image_cache[k].mount_point,
            image_cache[k].backend == ATTACH_BACKEND_LVD ? "lvd" : "md",
            image_cache[k].unit_id);
        first = false;
    }
    off += snprintf(buf + off, bufsz - off, "]");
    return off;
}

/* ============================================================
 * image_mount_unmount_by_path — find cache slot and unmount
 * ============================================================ */
bool image_mount_unmount_by_path(const char *path)
{
    for (int k = 0; k < MAX_IMAGE_MOUNTS; k++) {
        if (!image_cache[k].valid) continue;
        if (strcmp(image_cache[k].path, path) != 0) continue;
        bool ok = unmount_image(image_cache[k].path,
                                image_cache[k].unit_id,
                                image_cache[k].backend);
        if (ok) image_cache[k].valid = false;
        return ok;
    }
    log_debug("  [IMG] unmount_by_path: not found in cache: %s", path);
    return false;
}
