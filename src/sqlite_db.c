// thanks bucanero: https://github.com/bucanero/apollo-ps4/blob/main/source/sqlite_db.c

#include "sqlite_db.h"
#include "util.h"
#include "sfo.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sqlite3.h>

int sqlite3_memvfs_init(const char* vfsName);
int sqlite3_memvfs_dump(sqlite3 *db, const char *zSchema, const char *zFilename);

void* open_sqlite_db(const char* db_path)
{
    uint8_t* db_buf;
    size_t db_size;
    sqlite3* db;

    if (sqlite3_memvfs_init(NULL) != SQLITE_OK_LOAD_PERMANENTLY)
    {
        LOG("Error loading memvfs");
        return NULL;
    }

    if (read_buffer(db_path, &db_buf, &db_size) != SUCCESS)
    {
        LOG("Cannot open database file: %s", db_path);
        return NULL;
    }

    char *memuri = sqlite3_mprintf("file:memdb?ptr=0x%p&sz=%lld&freeonclose=1", db_buf, (long long)db_size);
    LOG("Opening '%s' (%zu bytes)...", db_path, db_size);

    if (sqlite3_open_v2(memuri, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, "memvfs") != SQLITE_OK)
    {
        LOG("Error open memvfs: %s", sqlite3_errmsg(db));
        sqlite3_free(memuri);
        return NULL;
    }
    sqlite3_free(memuri);

    if (sqlite3_exec(db, "PRAGMA journal_mode = OFF;", NULL, NULL, NULL) != SQLITE_OK)
        LOG("Error set pragma: %s", sqlite3_errmsg(db));

    return db;
}

int save_sqlite_db(void* db, const char* db_path)
{
    if (!db)
        return 0;

    LOG("Saving database to %s", db_path);
    if (sqlite3_memvfs_dump(db, NULL, db_path) != SQLITE_OK)
    {
        LOG("Error saving database: %s", sqlite3_errmsg(db));
        return 0;
    }

    return 1;
}

static void insert_appinfo_row(sqlite3* db, const char* titleId, const char* key, const char* value)
{
    char* query = sqlite3_mprintf("INSERT OR IGNORE INTO tbl_appinfo(titleId, key, val) VALUES(%Q, %Q, %Q)", titleId, key, value);

    if (sqlite3_exec(db, query, NULL, NULL, NULL) != SQLITE_OK)
        LOG("tbl_appinfo insert failed: %s", sqlite3_errmsg(db));

    sqlite3_free(query);
}

static void insert_appinfo_row_int(sqlite3* db, const char* titleId, const char* key, uint64_t value)
{
    char* query = sqlite3_mprintf("INSERT OR IGNORE INTO tbl_appinfo(titleId, key, val) VALUES(%Q, %Q, %llu)", titleId, key, (unsigned long long)value);

    if (sqlite3_exec(db, query, NULL, NULL, NULL) != SQLITE_OK)
        LOG("tbl_appinfo insert failed: %s", sqlite3_errmsg(db));

    sqlite3_free(query);
}

static void insert_appinfo_sfo_value(sqlite3* db, const char* titleId, const char* key, sfo_context_t* sfo)
{
    char* value = (char*) sfo_get_param_value(sfo, key);

    if (value)
        insert_appinfo_row(db, titleId, key, value);
}

static void insert_appinfo_sfo_value_int(sqlite3* db, const char* titleId, const char* key, sfo_context_t* sfo)
{
    void* value = sfo_get_param_value(sfo, key);
    uint32_t tmp = value ? *(uint32_t*)value : 0;
    insert_appinfo_row_int(db, titleId, key, tmp);
}

static int rebuild_appbrowse_table(sqlite3* db, const char* table)
{
    DIR* dp;
    struct dirent* dirp;
    char path[512];
    sfo_context_t* sfo;
    uint64_t pkg_size;
    int added = 0;

    dp = opendir("/user/app");
    if (!dp)
    {
        LOG("Failed to open /user/app/");
        return 0;
    }

    while ((dirp = readdir(dp)) != NULL)
    {
        if (dirp->d_type != DT_DIR || strlen(dirp->d_name) != 9)
            continue;

        sqlite3_stmt* res;
        snprintf(path, sizeof(path), "SELECT 1 FROM %s WHERE titleId='%s'", table, dirp->d_name);
        if (sqlite3_prepare_v2(db, path, -1, &res, NULL) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW)
        {
            LOG("(%s) already in %s", dirp->d_name, table);
            sqlite3_finalize(res);
            continue;
        }
        sqlite3_finalize(res);

        snprintf(path, sizeof(path), "/user/app/%s/app.pkg", dirp->d_name);
        if (get_file_size(path, &pkg_size) != SUCCESS)
        {
            LOG("Failed to get %s file size: %s", dirp->d_name, path);
            continue;
        }

        snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.sfo", dirp->d_name);
        sfo = sfo_alloc();
        if (sfo_read(sfo, path) != 0)
        {
            LOG("Unable to read from '%s'", path);
            sfo_free(sfo);
            continue;
        }

        char *sfo_title = (char*) sfo_get_param_value(sfo, "TITLE");
        char *sfo_titleid = (char*) sfo_get_param_value(sfo, "TITLE_ID");
        char *sfo_content = (char*) sfo_get_param_value(sfo, "CONTENT_ID");
        char *sfo_category = (char*) sfo_get_param_value(sfo, "CATEGORY");

        if (!sfo_title || !sfo_titleid || !sfo_content || !sfo_category)
        {
            LOG("Missing SFO fields for %s", dirp->d_name);
            sfo_free(sfo);
            continue;
        }

        if (strcmp(sfo_category, "gp") == 0)
            sfo_category = "gd";

        LOG("Adding (%s) %s '%s' to %s...", sfo_titleid, sfo_content, sfo_title, table);

        char* query = sqlite3_mprintf("INSERT INTO %s (titleId, contentId, titleName,"
            "metaDataPath, lastAccessTime, contentStatus, onDisc, parentalLevel, visible,"
            "sortPriority, pathInfo, lastAccessIndex, dispLocation, canRemove, category, contentType, pathInfo2,"
            "presentBoxStatus, entitlement, thumbnailUrl, lastUpdateTime, playableDate, contentSize,"
            "installDate, platform, uiCategory, skuId, disableLiveDetail, linkType, linkUri,"
            "serviceIdAddCont1, serviceIdAddCont2, serviceIdAddCont3, serviceIdAddCont4,"
            "serviceIdAddCont5, serviceIdAddCont6, serviceIdAddCont7, folderType, folderInfo,"
            "parentFolderId, positionInFolder, activeDate, entitlementTitleName, hddLocation,"
            "externalHddAppStatus, entitlementIdKamaji, mTime) VALUES (%Q, %Q, %Q, '/user/appmeta/%s',"
            "'2020-01-01 20:20:03.000', 0, 0, 5, 1, 100, 0, 1, 5, 1, %Q, 0, 0, 0, 0, NULL, NULL, NULL, %llu,"
            "'2020-01-01 20:20:01.000', 0, %Q, NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,"
            "0, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, '2020-01-01 20:20:02.000')",
            table, sfo_titleid, sfo_content, sfo_title, dirp->d_name, sfo_category,
            (unsigned long long)pkg_size,
            (strcmp(sfo_category, "gde") == 0) ? "app" : "game");

        if (sqlite3_exec(db, query, NULL, NULL, NULL) != SQLITE_OK)
        {
            LOG("Error inserting '%s': %s", sfo_titleid, sqlite3_errmsg(db));
            sqlite3_free(query);
            sfo_free(sfo);
            continue;
        }

        LOG("Adding (%s) attributes to tbl_appinfo...", sfo_titleid);
        insert_appinfo_row(db, sfo_titleid, "TITLE", sfo_title);
        insert_appinfo_row(db, sfo_titleid, "TITLE_ID", sfo_titleid);
        insert_appinfo_row(db, sfo_titleid, "CATEGORY", sfo_category);
        insert_appinfo_row(db, sfo_titleid, "CONTENT_ID", sfo_content);

        insert_appinfo_sfo_value(db, sfo_titleid, "APP_VER", sfo);
        insert_appinfo_sfo_value(db, sfo_titleid, "VERSION", sfo);
        insert_appinfo_sfo_value(db, sfo_titleid, "FORMAT", sfo);
        insert_appinfo_sfo_value(db, sfo_titleid, "INSTALL_DIR_SAVEDATA", sfo);

        insert_appinfo_sfo_value_int(db, sfo_titleid, "APP_TYPE", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "ATTRIBUTE", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "PARENTAL_LEVEL", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "SYSTEM_VER", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "DOWNLOAD_DATA_SIZE", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "REMOTE_PLAY_KEY_ASSIGN", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "ATTRIBUTE_INTERNAL", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "DISP_LOCATION_1", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "DISP_LOCATION_2", sfo);
        insert_appinfo_sfo_value_int(db, sfo_titleid, "PT_PARAM", sfo);

        for (int i = 1; i < 5; i++)
        {
            snprintf(path, sizeof(path), "USER_DEFINED_PARAM_%d", i);
            insert_appinfo_sfo_value_int(db, sfo_titleid, path, sfo);
        }
        for (int i = 1; i < 8; i++)
        {
            snprintf(path, sizeof(path), "SERVICE_ID_ADDCONT_ADD_%d", i);
            insert_appinfo_sfo_value(db, sfo_titleid, path, sfo);

            snprintf(path, sizeof(path), "SAVE_DATA_TRANSFER_TITLE_ID_LIST_%d", i);
            insert_appinfo_sfo_value(db, sfo_titleid, path, sfo);
        }
        for (int i = 0; i < 30; i++)
        {
            snprintf(path, sizeof(path), "TITLE_%02d", i);
            insert_appinfo_sfo_value(db, sfo_titleid, path, sfo);
        }

        snprintf(path, sizeof(path), "/user/appmeta/%s", dirp->d_name);
        insert_appinfo_row(db, sfo_titleid, "_metadata_path", path);

        snprintf(path, sizeof(path), "/user/app/%s", dirp->d_name);
        insert_appinfo_row(db, sfo_titleid, "_org_path", path);

        insert_appinfo_row_int(db, sfo_titleid, "#_size", pkg_size);
        insert_appinfo_row_int(db, sfo_titleid, "#_contents_status", 0);
        insert_appinfo_row_int(db, sfo_titleid, "#_access_index", 1);
        insert_appinfo_row_int(db, sfo_titleid, "_contents_location", 0);
        insert_appinfo_row_int(db, sfo_titleid, "#_update_index", 74);
        insert_appinfo_row_int(db, sfo_titleid, "#exit_type", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_contents_ext_type", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_current_slot", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_disable_live_detail", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_hdd_location", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_path_info", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_path_info_2", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_size_other_hdd", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_sort_priority", 100);
        insert_appinfo_row_int(db, sfo_titleid, "_uninstallable", 1);
        insert_appinfo_row_int(db, sfo_titleid, "_view_category", 0);
        insert_appinfo_row_int(db, sfo_titleid, "_working_status", 0);
        insert_appinfo_row_int(db, sfo_titleid, "#_booted", 1);
        insert_appinfo_row(db, sfo_titleid, "#_last_access_time", "2020-01-01 20:20:03.000");
        insert_appinfo_row(db, sfo_titleid, "#_mtime", "2020-01-01 20:20:02.000");
        insert_appinfo_row(db, sfo_titleid, "#_promote_time", "2020-01-01 20:20:01.000");

        sqlite3_free(query);
        sfo_free(sfo);
        added++;
    }

    closedir(dp);
    LOG("Added %d entries to %s", added, table);
    return 1;
}

int appdb_rebuild(const char* db_path)
{
    sqlite3* db;
    sqlite3_stmt* stmt;
    int rc;
    int ret = 1;

    db = open_sqlite_db(db_path);
    if (!db)
        return 0;

    rc = sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_appbrowse_%'", -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        LOG("Failed to list appbrowse tables: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        LOG("No tbl_appbrowse_* tables found in %s", db_path);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 0;
    }

    do
    {
        const char* table = (const char*)sqlite3_column_text(stmt, 0);
        LOG("Rebuilding %s...", table);
        if (!rebuild_appbrowse_table(db, table))
            ret = 0;
    } while (sqlite3_step(stmt) == SQLITE_ROW);

    sqlite3_finalize(stmt);

    LOG("Saving database to %s", db_path);
    if (!save_sqlite_db(db, db_path))
        ret = 0;

    sqlite3_close(db);
    return ret;
}

int addcont_dlc_rebuild(const char* db_path)
{
    char path[512];
    sfo_context_t* sfo;
    DIR *dp, *dp2;
    struct dirent *dirp, *dirp2;
    sqlite3* db;
    char* query;
    int ret = 1;

    dp = opendir("/user/addcont");
    if (!dp)
    {
        LOG("Failed to open /user/addcont/");
        return 0;
    }

    db = open_sqlite_db(db_path);
    if (!db)
    {
        closedir(dp);
        return 0;
    }

    while ((dirp = readdir(dp)) != NULL)
    {
        if (dirp->d_type != DT_DIR || strlen(dirp->d_name) != 9)
            continue;

        snprintf(path, sizeof(path), "/user/addcont/%s", dirp->d_name);
        dp2 = opendir(path);
        if (!dp2)
            continue;

        while ((dirp2 = readdir(dp2)) != NULL)
        {
            if (dirp2->d_type != DT_DIR || strlen(dirp2->d_name) != 16)
                continue;

            snprintf(path, sizeof(path), "/user/addcont/%s/%s/ac.pkg", dirp->d_name, dirp2->d_name);
            sfo = sfo_alloc();
            if (sfo_read(sfo, path) != 0)
            {
                LOG("Unable to read from '%s'", path);
                sfo_free(sfo);
                continue;
            }

            LOG("Adding %s to addcont...", dirp2->d_name);
            query = sqlite3_mprintf("INSERT OR IGNORE INTO addcont(title_id, dir_name, content_id, title, version, attribute, status) "
                "VALUES(%Q, %Q, %Q, %Q, 536870912, '01.00', 0)",
                dirp->d_name, dirp2->d_name, sfo_get_param_value(sfo, "CONTENT_ID"), sfo_get_param_value(sfo, "TITLE"));

            if (sqlite3_exec(db, query, NULL, NULL, NULL) != SQLITE_OK)
                LOG("addcont insert failed: %s", sqlite3_errmsg(db));

            sqlite3_free(query);
            sfo_free(sfo);
        }
        closedir(dp2);
    }
    closedir(dp);

    LOG("Saving database to %s", db_path);
    if (!save_sqlite_db(db, db_path))
        ret = 0;

    sqlite3_close(db);
    return ret;
}
