/* install.h -- VoidShell homebrew installation pipeline */
#ifndef INSTALL_H
#define INSTALL_H

#include <stdbool.h>

int  copy_file(const char *src, const char *dst);
void copy_dir(const char *src, const char *dst);
void copy_assets_to_appmeta(const char *assets_src_dir, const char *title_id);
void fix_permissions(const char *dir);
void mount_nullfs(const char *src, const char *dst);
void remount_system_ex(void);
bool has_been_fixed(const char *title_id);
void mark_as_fixed(const char *title_id, const char *content);
void mount_and_install(const char *src_path, const char *title_id,
                       const char *display_name, int skip_sce_sys);
int  find_user_tables_callback(void *unused, int argc, char **argv, char **col_names);
void update_app_db(const char *title_id);
int  get_game_info_extended(const char *app_path, void *info_out);
int  get_game_info(const char *app_path, char *title_id_out, char *title_name_out);
bool is_data_mounted(const char *title_id);
bool is_installed(const char *title_id);
void *thread_installer_worker(void *arg);

#endif /* INSTALL_H */
