#ifndef IMAGE_MOUNT_H
#define IMAGE_MOUNT_H
#include "types.h"

void image_mount_init(void);
void image_mount_scan(void);
void image_mount_shutdown(void);
int  image_mount_list_json(char *buf, int bufsz);
bool image_mount_unmount_by_path(const char *path);

#endif /* IMAGE_MOUNT_H */
