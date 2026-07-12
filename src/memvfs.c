/*
** 2016-09-07
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This is an in-memory VFS implementation.  The application supplies
** a chunk of memory to hold the database file.
**
** Because there is place to store a rollback or wal journal, the database
** must use one of journal_mode=MEMORY or journal_mode=OFF.
*/
#include <sqlite3.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct sqlite3_vfs MemVfs;
typedef struct MemFile MemFile;

#define ORIGVFS(p) ((sqlite3_vfs*)((p)->pAppData))

struct MemFile {
  sqlite3_file base;
  sqlite3_int64 sz;
  sqlite3_int64 szMax;
  unsigned char *aData;
  int bFreeOnClose;
};

static int memClose(sqlite3_file*);
static int memRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int memWrite(sqlite3_file*, const void*, int iAmt, sqlite3_int64 iOfst);
static int memTruncate(sqlite3_file*, sqlite3_int64 size);
static int memSync(sqlite3_file*, int flags);
static int memFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int memLock(sqlite3_file*, int);
static int memUnlock(sqlite3_file*, int);
static int memCheckReservedLock(sqlite3_file*, int *pResOut);
static int memFileControl(sqlite3_file*, int op, void *pArg);
static int memSectorSize(sqlite3_file*);
static int memDeviceCharacteristics(sqlite3_file*);
static int memShmMap(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
static int memShmLock(sqlite3_file*, int offset, int n, int flags);
static void memShmBarrier(sqlite3_file*);
static int memShmUnmap(sqlite3_file*, int deleteFlag);
static int memFetch(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
static int memUnfetch(sqlite3_file*, sqlite3_int64 iOfst, void *p);

static int memOpen(sqlite3_vfs*, const char *, sqlite3_file*, int , int *);
static int memDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int memAccess(sqlite3_vfs*, const char *zName, int flags, int *);
static int memFullPathname(sqlite3_vfs*, const char *zName, int, char *zOut);
static void *memDlOpen(sqlite3_vfs*, const char *zFilename);
static void memDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void (*memDlSym(sqlite3_vfs *pVfs, void *p, const char*zSym))(void);
static void memDlClose(sqlite3_vfs*, void*);
static int memRandomness(sqlite3_vfs*, int nByte, char *zBufOut);
static int memSleep(sqlite3_vfs*, int microseconds);
static int memCurrentTime(sqlite3_vfs*, double*);
static int memGetLastError(sqlite3_vfs*, int, char *);
static int memCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);

static sqlite3_vfs mem_vfs = {
  2,
  0,
  1024,
  0,
  "memvfs",
  0,
  memOpen,
  memDelete,
  memAccess,
  memFullPathname,
  memDlOpen,
  memDlError,
  memDlSym,
  memDlClose,
  memRandomness,
  memSleep,
  memCurrentTime,
  memGetLastError,
  memCurrentTimeInt64
};

static const sqlite3_io_methods mem_io_methods = {
  3,
  memClose,
  memRead,
  memWrite,
  memTruncate,
  memSync,
  memFileSize,
  memLock,
  memUnlock,
  memCheckReservedLock,
  memFileControl,
  memSectorSize,
  memDeviceCharacteristics,
  memShmMap,
  memShmLock,
  memShmBarrier,
  memShmUnmap,
  memFetch,
  memUnfetch
};

static int memClose(sqlite3_file *pFile){
  MemFile *p = (MemFile *)pFile;
  if( p->bFreeOnClose ) free(p->aData);
  return SQLITE_OK;
}

static int memRead(
  sqlite3_file *pFile,
  void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
  MemFile *p = (MemFile *)pFile;
  memcpy(zBuf, p->aData+iOfst, iAmt);
  return SQLITE_OK;
}

static int memWrite(
  sqlite3_file *pFile,
  const void *z,
  int iAmt,
  sqlite_int64 iOfst
){
  MemFile *p = (MemFile *)pFile;
  if( iOfst+iAmt>p->sz ){
    if( iOfst+iAmt>p->szMax ){
      void* ptr = realloc(p->aData, iOfst+iAmt);
      if( !ptr ) return SQLITE_FULL;
      p->szMax = iOfst+iAmt;
      p->aData = ptr;
    }
    if( iOfst>p->sz ) memset(p->aData+p->sz, 0, iOfst-p->sz);
    p->sz = iOfst+iAmt;
  }
  memcpy(p->aData+iOfst, z, iAmt);
  return SQLITE_OK;
}

static int memTruncate(sqlite3_file *pFile, sqlite_int64 size){
  MemFile *p = (MemFile *)pFile;
  if( size>p->sz ){
    if( size>p->szMax ) return SQLITE_FULL;
    memset(p->aData+p->sz, 0, size-p->sz);
  }
  p->sz = size;
  return SQLITE_OK;
}

static int memSync(sqlite3_file *pFile, int flags){
  return SQLITE_OK;
}

static int memFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize){
  MemFile *p = (MemFile *)pFile;
  *pSize = p->sz;
  return SQLITE_OK;
}

static int memLock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}

static int memUnlock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}

static int memCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  *pResOut = 0;
  return SQLITE_OK;
}

static int memFileControl(sqlite3_file *pFile, int op, void *pArg){
  MemFile *p = (MemFile *)pFile;
  int rc = SQLITE_NOTFOUND;
  if( op==SQLITE_FCNTL_VFSNAME ){
    *(char**)pArg = sqlite3_mprintf("mem(%p,%lld)", p->aData, p->sz);
    rc = SQLITE_OK;
  }
  return rc;
}

static int memSectorSize(sqlite3_file *pFile){
  return 1024;
}

static int memDeviceCharacteristics(sqlite3_file *pFile){
  return SQLITE_IOCAP_ATOMIC |
         SQLITE_IOCAP_POWERSAFE_OVERWRITE |
         SQLITE_IOCAP_SAFE_APPEND |
         SQLITE_IOCAP_SEQUENTIAL;
}

static int memShmMap(
  sqlite3_file *pFile,
  int iPg,
  int pgsz,
  int bExtend,
  void volatile **pp
){
  return SQLITE_IOERR_SHMMAP;
}

static int memShmLock(sqlite3_file *pFile, int offset, int n, int flags){
  return SQLITE_IOERR_SHMLOCK;
}

static void memShmBarrier(sqlite3_file *pFile){
  return;
}

static int memShmUnmap(sqlite3_file *pFile, int deleteFlag){
  return SQLITE_OK;
}

static int memFetch(
  sqlite3_file *pFile,
  sqlite3_int64 iOfst,
  int iAmt,
  void **pp
){
  MemFile *p = (MemFile *)pFile;
  *pp = (void*)(p->aData + iOfst);
  return SQLITE_OK;
}

static int memUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage){
  return SQLITE_OK;
}

static int memOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  MemFile *p = (MemFile*)pFile;
  memset(p, 0, sizeof(*p));
  if( (flags & SQLITE_OPEN_MAIN_DB)==0 ) return SQLITE_CANTOPEN;
  p->aData = (unsigned char*)sqlite3_uri_int64(zName,"ptr",0);
  if( p->aData==0 ) return SQLITE_CANTOPEN;
  p->sz = sqlite3_uri_int64(zName,"sz",0);
  if( p->sz<0 ) return SQLITE_CANTOPEN;
  p->szMax = sqlite3_uri_int64(zName,"max",p->sz);
  if( p->szMax<p->sz ) return SQLITE_CANTOPEN;
  p->bFreeOnClose = sqlite3_uri_boolean(zName,"freeonclose",0);
  pFile->pMethods = &mem_io_methods;
  return SQLITE_OK;
}

static int memDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
  return SQLITE_IOERR_DELETE;
}

static int memAccess(
  sqlite3_vfs *pVfs,
  const char *zPath,
  int flags,
  int *pResOut
){
  *pResOut = 0;
  return SQLITE_OK;
}

static int memFullPathname(
  sqlite3_vfs *pVfs,
  const char *zPath,
  int nOut,
  char *zOut
){
  sqlite3_snprintf(nOut, zOut, "%s", zPath);
  return SQLITE_OK;
}

static void *memDlOpen(sqlite3_vfs *pVfs, const char *zPath){
  return ORIGVFS(pVfs)->xDlOpen(ORIGVFS(pVfs), zPath);
}

static void memDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
  ORIGVFS(pVfs)->xDlError(ORIGVFS(pVfs), nByte, zErrMsg);
}

static void (*memDlSym(sqlite3_vfs *pVfs, void *p, const char *zSym))(void){
  return ORIGVFS(pVfs)->xDlSym(ORIGVFS(pVfs), p, zSym);
}

static void memDlClose(sqlite3_vfs *pVfs, void *pHandle){
  ORIGVFS(pVfs)->xDlClose(ORIGVFS(pVfs), pHandle);
}

static int memRandomness(sqlite3_vfs *pVfs, int nByte, char *zBufOut){
  return ORIGVFS(pVfs)->xRandomness(ORIGVFS(pVfs), nByte, zBufOut);
}

static int memSleep(sqlite3_vfs *pVfs, int nMicro){
  return ORIGVFS(pVfs)->xSleep(ORIGVFS(pVfs), nMicro);
}

static int memCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut){
  return ORIGVFS(pVfs)->xCurrentTime(ORIGVFS(pVfs), pTimeOut);
}

static int memGetLastError(sqlite3_vfs *pVfs, int a, char *b){
  return ORIGVFS(pVfs)->xGetLastError(ORIGVFS(pVfs), a, b);
}

static int memCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *p){
  double ct = 0;
  memCurrentTime(pVfs, &ct);
  *p = (sqlite3_int64)(ct * 86400000);
  return SQLITE_OK;
}

int sqlite3_memvfs_init(
  const char* vfsName
){
  static int rc = SQLITE_OK;
  if( rc==SQLITE_OK_LOAD_PERMANENTLY ){
    return rc;
  }
  sqlite3_initialize();
  mem_vfs.pAppData = sqlite3_vfs_find(vfsName);
  if( mem_vfs.pAppData==0 ) return SQLITE_ERROR;
  mem_vfs.szOsFile = sizeof(MemFile);
  rc = sqlite3_vfs_register(&mem_vfs, 1);
  if( rc==SQLITE_OK ) rc = SQLITE_OK_LOAD_PERMANENTLY;
  return rc;
}

int sqlite3_memvfs_dump(
  sqlite3 *db,
  const char *zSchema,
  const char *zFilename
){
  MemFile *p = 0;
  FILE *out;
  int rc;
  sqlite3_vfs *pVfs = 0;

  if( zFilename==0 ) return SQLITE_ERROR;
  out = fopen(zFilename, "wb");
  if( out==0 ) return SQLITE_ERROR;
  rc = sqlite3_file_control(db, zSchema, SQLITE_FCNTL_VFS_POINTER, &pVfs);
  if( rc || pVfs==0 ) return SQLITE_ERROR;
  if( strcmp(pVfs->zName,"memvfs")!=0 ) return SQLITE_ERROR;
  rc = sqlite3_file_control(db, zSchema, SQLITE_FCNTL_FILE_POINTER, &p);
  if( rc ) return SQLITE_ERROR;
  fwrite(p->aData, 1, (size_t)p->sz, out);
  fclose(out);
  return SQLITE_OK;
}
