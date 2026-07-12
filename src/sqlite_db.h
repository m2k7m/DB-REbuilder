#ifndef _SQLITE_DB_H_
#define _SQLITE_DB_H_

#define APP_DB_PATH     "/system_data/priv/mms/app.db"
#define ADDCONT_DB_PATH "/system_data/priv/mms/addcont.db"

void* open_sqlite_db(const char* db_path);
int save_sqlite_db(void* db, const char* db_path);
int appdb_rebuild(const char* db_path);
int addcont_dlc_rebuild(const char* db_path);

#endif /* !_SQLITE_DB_H_ */
