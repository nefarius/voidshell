/* install.c -- VoidShell homebrew installation pipeline */
#include "install.h"
#include "types.h"
#include "logging.h"
#include "utils.h"
#include "kernel.h"
#include "ps5_sdk.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <pthread.h>

int  user_table_count = 0;
char user_tables[10][0x100];

void pkg_log(const char *fmt, ...); /* defined in webserver.c */

#ifndef MNT_NULLFS
#define MNT_NULLFS 1
#endif

/* ---- copy_file  (0x1005b0) ---- */
int copy_file(const char *src, const char *dst)
{
    char  *buf;
    FILE  *fin, *fout;
    size_t n;

    buf = malloc(0x8000);
    if (!buf) return -1;
    fin = fopen(src, "rb");
    if (!fin) { free(buf); return -1; }
    fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); free(buf); return -1; }
    while ((n = fread(buf, 1, 0x8000, fin)) > 0)
        fwrite(buf, 1, n, fout);
    fclose(fout); fclose(fin); free(buf);
    return 0;
}

/* ---- fix_permissions  (0x1006c0) ---- */
void fix_permissions(const char *dir)
{
    char path[1024];
    chmod(dir, 0777);
    snprintf(path, sizeof(path), "%s/icon0.png", dir); chmod(path, 0777);
    snprintf(path, sizeof(path), "%s/snd0.at9",  dir); chmod(path, 0777);
    snprintf(path, sizeof(path), "%s/pic0.png",  dir); chmod(path, 0777);
}

/* ---- copy_assets_to_appmeta  (0x100770) ---- */
void copy_assets_to_appmeta(const char *src_dir, const char *title_id)
{
    char appmeta[1024], src[1024], dst[1024];
    snprintf(appmeta, sizeof(appmeta), "/user/appmeta/%s", title_id);
    mkdir(appmeta, 0777); chmod(appmeta, 0777);
    snprintf(src,sizeof(src),"%s/icon0.png",src_dir); snprintf(dst,sizeof(dst),"%s/icon0.png",appmeta); copy_file(src,dst);
    snprintf(src,sizeof(src),"%s/pic0.png", src_dir); snprintf(dst,sizeof(dst),"%s/pic0.png", appmeta); copy_file(src,dst);
    snprintf(src,sizeof(src),"%s/snd0.at9", src_dir); snprintf(dst,sizeof(dst),"%s/snd0.at9", appmeta); copy_file(src,dst);
    chmod(appmeta,0777);
    snprintf(dst,sizeof(dst),"%s/icon0.png",appmeta); chmod(dst,0777);
    snprintf(dst,sizeof(dst),"%s/snd0.at9", appmeta); chmod(dst,0777);
    snprintf(dst,sizeof(dst),"%s/pic0.png", appmeta); chmod(dst,0777);
}

/* ---- copy_dir  (0x101ea0) ---- */
void copy_dir(const char *src, const char *dst)
{
    DIR *dir; struct dirent *entry; struct stat st; char sp[1024], dp[1024];
    if (g_paused) return;
    mkdir(dst, 0777);
    dir = opendir(src); if (!dir) return;
    while (!g_paused && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name,".")==0 || strcmp(entry->d_name,"..")==0) continue;
        snprintf(sp,sizeof(sp),"%s/%s",src,entry->d_name);
        snprintf(dp,sizeof(dp),"%s/%s",dst,entry->d_name);
        if (stat(sp,&st)!=0) continue;
        if (S_ISDIR(st.st_mode)) copy_dir(sp,dp); else copy_file(sp,dp);
    }
    closedir(dir);
}

/* ---- get_game_info_extended  (0x101370) ---- */
int get_game_info_extended(const char *app_path, void *info_out)
{
    typedef struct { char titleId[0x20]; char titleName[0x100]; char contentVersion[0x10]; char regionCode[5]; } GI;
    GI *info = (GI *)info_out;
    char pp[1024]; FILE *fp; long sz; char *buf; const char *ts;

    snprintf(pp, sizeof(pp), "%s/sce_sys/param.json", app_path);
    fp = fopen(pp, "rb"); if (!fp) return -1;
    fseek(fp,0,SEEK_END); sz=ftell(fp); fseek(fp,0,SEEK_SET);
    if (sz<=0||sz>0x4FFFFF) { fclose(fp); return -1; }
    buf = malloc(sz+1); if (!buf) { fclose(fp); return -1; }
    fread(buf,1,sz,fp); buf[sz]='\0'; fclose(fp);

    if (extract_json_string(buf,"titleId",   info->titleId,   0x20)!=0)
        extract_json_string(buf,"title_id",  info->titleId,   0x20);
    ts = strstr(buf,"\"en-US\""); if (!ts) ts=buf;
    if (extract_json_string(ts, "titleName", info->titleName, 0x100)!=0)
        extract_json_string(buf,"titleName", info->titleName, 0x100);
    if (extract_json_string(buf,"contentVersion",info->contentVersion,0x10)!=0)
        if (extract_json_string(buf,"masterVersion",info->contentVersion,0x10)!=0)
            strncpy(info->contentVersion,"01.00",0x10);
    if (strlen(info->titleId)>=4) { strncpy(info->regionCode,info->titleId,4); info->regionCode[4]='\0'; }
    else strncpy(info->regionCode,"UNKN",5);
    free(buf);
    return (info->titleId[0]!='\0') ? 0 : -1;
}

/* ---- get_game_info  (0x101670) ---- */
int get_game_info(const char *app_path, char *tid, char *tname)
{
    typedef struct { char titleId[0x20]; char titleName[0x100]; char contentVersion[0x10]; char regionCode[5]; } GI;
    GI info; int rc;
    memset(&info,0,sizeof(info));
    rc = get_game_info_extended(app_path,&info);
    if (rc==0) { strncpy(tid,info.titleId,0x20); strncpy(tname,info.titleName,0x100); }
    return rc;
}

/* ---- find_user_tables_callback  (0x101730) ---- */
int find_user_tables_callback(void *u, int argc, char **argv, char **cn)
{
    (void)u; (void)cn;
    if (argc>0 && argv[0] && user_table_count<10) {
        strncpy(user_tables[user_table_count],argv[0],0xFF);
        user_tables[user_table_count++][0xFF]='\0';
    }
    return 0;
}

/* ---- update_app_db  (0x101770) ---- */
void update_app_db(const char *tid)
{
    sqlite3 *db; char sql[2048]; char *err=NULL; int i;
    if (!g_enable_db_patch) return;
    if (sqlite3_open_v2("/system_data/priv/mms/app.db",&db,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK) return;
    snprintf(sql,sizeof(sql),
        "BEGIN TRANSACTION; UPDATE tbl_contentinfo SET "
        "metaDataPath='/user/appmeta/%s',snd0Info='/user/appmeta/%s/snd0.at9',"
        "icon0Info='/user/appmeta/%s/icon0.png',pic0Info='/user/appmeta/%s/pic0.png' "
        "WHERE titleId='%s'; COMMIT;", tid,tid,tid,tid,tid);
    sqlite3_exec(db,sql,NULL,NULL,&err); if(err)sqlite3_free(err);
    user_table_count=0;
    sqlite3_exec(db,"SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_concepticoninfo_%';",
                 find_user_tables_callback,NULL,NULL);
    for (i=0;i<user_table_count;i++) {
        snprintf(sql,sizeof(sql),
            "BEGIN TRANSACTION; UPDATE %s SET "
            "metaDataPath='/user/appmeta/%s',snd0Info='/user/appmeta/%s/snd0.at9',"
            "icon0Info='/user/appmeta/%s/icon0.png',pic0Info='/user/appmeta/%s/pic0.png' "
            "WHERE primaryTitleId='%s'; COMMIT;",
            user_tables[i],tid,tid,tid,tid,tid);
        err=NULL; sqlite3_exec(db,sql,NULL,NULL,&err); if(err)sqlite3_free(err);
    }
    sqlite3_close(db);
}

/* ---- mount helpers ---- */
void mount_nullfs(const char *src, const char *dst)
{
    struct iovec iov[6];
    iov[0].iov_base="fstype"; iov[0].iov_len=7;
    iov[1].iov_base="nullfs"; iov[1].iov_len=7;
    iov[2].iov_base="from";   iov[2].iov_len=5;
    iov[3].iov_base=(void*)src; iov[3].iov_len=strlen(src)+1;
    iov[4].iov_base="fspath"; iov[4].iov_len=7;
    iov[5].iov_base=(void*)dst; iov[5].iov_len=strlen(dst)+1;
    nmount(iov,6,MNT_NULLFS);
}

void remount_system_ex(void)
{
    /* TODO: decompile addr 0x101960 — recover full iovec from data segment */
    struct iovec iov[4];
    iov[0].iov_base="fstype";     iov[0].iov_len=7;
    iov[1].iov_base="exfatfs";    iov[1].iov_len=8;
    iov[2].iov_base="fspath";     iov[2].iov_len=7;
    iov[3].iov_base="/system_ex"; iov[3].iov_len=11;
    nmount(iov,4,0x10000);
}

/* ---- has_been_fixed / mark_as_fixed ---- */
bool has_been_fixed(const char *title_id)
{
    char m[1024]; snprintf(m,sizeof(m),"/data/voidshell/history/%s",title_id);
    return (access(m,F_OK)==0);
}
void mark_as_fixed(const char *title_id, const char *content)
{
    char m[1024]; FILE *fp;
    snprintf(m,sizeof(m),"/data/voidshell/history/%s",title_id);
    fp=fopen(m,"w"); if(!fp) return;
    fputs(content,fp); fclose(fp);
}

/* ---- is_data_mounted / is_installed ---- */
bool is_data_mounted(const char *tid)
{
    char p[1024]; snprintf(p,sizeof(p),"/system_ex/app/%s/sce_sys/param.json",tid);
    return (access(p,F_OK)==0);
}
bool is_installed(const char *tid)
{
    char p[1024]; struct stat st; snprintf(p,sizeof(p),"/user/app/%s",tid);
    return (stat(p,&st)==0);
}

/* ---- mount_and_install  (0x101ad0) ---- */
void mount_and_install(const char *src_path, const char *tid,
                       const char *display_name, int skip_sce_sys)
{
    char sys_ex[1024],ua[1024],ss[1024],sd[1024],is[1024],id2[1024],hist[1024];
    struct iovec iov[6]; int rc;

    if (g_security_trip) return;
    snprintf(sys_ex,sizeof(sys_ex),"/system_ex/app/%s",tid);
    mkdir(sys_ex,0777);
    remount_system_ex();
    unmount(sys_ex,0);

    iov[0].iov_base="fstype"; iov[0].iov_len=7;
    iov[1].iov_base="nullfs"; iov[1].iov_len=7;
    iov[2].iov_base="from";   iov[2].iov_len=5;
    iov[3].iov_base=(void*)src_path; iov[3].iov_len=strlen(src_path)+1;
    iov[4].iov_base="fspath"; iov[4].iov_len=7;
    iov[5].iov_base=sys_ex;   iov[5].iov_len=strlen(sys_ex)+1;
    rc=nmount(iov,6,MNT_NULLFS);
    if (rc<0) return;

    if (!skip_sce_sys) {
        snprintf(ua, sizeof(ua), "/user/app/%s",          tid);
        snprintf(sd, sizeof(sd), "%s/sce_sys",            ua);
        snprintf(ss, sizeof(ss), "%s/sce_sys",            src_path);
        snprintf(is, sizeof(is), "%s/sce_sys/icon0.png",  src_path);
        snprintf(id2,sizeof(id2),"%s/icon0.png",          ua);
        mkdir(ua,0777); mkdir(sd,0777);
        copy_dir(ss,sd); copy_file(is,id2);
        copy_assets_to_appmeta(ss,tid);
        rc=sceAppInstUtilAppInstallTitleDir(tid,"/user/app/",0);
        sceKernelUsleep(200000);
        if (rc!=0 && rc!=(int)0x80870002) return;
        notify_system("Installed: %s",display_name);
        if (g_session_count<200) {
            strncpy(g_session_installed[g_session_count],tid,0x1f);
            g_session_count++;
        }
    }
    snprintf(hist,sizeof(hist),"/data/voidshell/history/%s",tid);
    if (access(hist,F_OK)!=0) {
        sceKernelUsleep(2000000);
        update_app_db(tid);
        FILE *fp=fopen(hist,"w");
        if(fp){fputs(src_path,fp);fclose(fp);}
    }
    g_library_dirty=1;
}

/* ---- thread_installer_worker  (0x105e80) ---- */
void *thread_installer_worker(void *arg)
{
    char  *pkg_path=(char*)arg;
    char   base[256];
    const char *bn;
    char  *p;
    int    rc;
    char   info_buf[0x2700];
    struct { const char *path; char pad[0x18]; } params;
    char   prog[0x18];

    if (!pkg_path) return NULL;
    p=strrchr(pkg_path,'/'); bn=p?p+1:pkg_path;
    strncpy(base,bn,sizeof(base)-1); base[sizeof(base)-1]='\0';

    pkg_log("Worker: Starting install for %s",base);
    chmod(pkg_path,0777);
    pkg_log("Loading AppInst Module...");
    sceSysmoduleLoadModule(0x80A);
    sceAppInstUtilInitialize();
    memset(&params,0,sizeof(params)); memset(info_buf,0,sizeof(info_buf)); memset(prog,0,sizeof(prog));
    params.path=pkg_path;
    pkg_log("Installing from Path: %s",pkg_path);
    notify_system("Installation Started: %s",base);
    rc=sceAppInstUtilInstallByPackage(&params,prog,info_buf);
    if (rc==0) { pkg_log("Success! Added to Downloads."); notify_system("Install Queued Successfully"); }
    else { pkg_log("Install Failed: Error 0x%X",rc); notify_system("Install Failed: 0x%X",rc); }
    sceAppInstUtilTerminate();
    sceSysmoduleUnloadModule(0x80A);
    free(pkg_path);
    return NULL;
}
