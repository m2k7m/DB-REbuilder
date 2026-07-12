#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "util.h"
#include "sqlite_db.h"

#ifdef BUILD_INSTALLER
#include "payload_elf.h"
#define PAYLOAD_DST "/data/payloads/db-rebuilder.elf"

static int install_payload(void)
{
    const unsigned char* start = _binary_payload_normal_elf_start;
    const unsigned char* end = _binary_payload_normal_elf_end;
    size_t size = end - start;
    FILE* fp;

    LOG("Installing payload to %s (%zu bytes)", PAYLOAD_DST, size);

    if (mkdirs(PAYLOAD_DST) != SUCCESS)
    {
        LOG("Failed to create /data/payloads/");
        return -1;
    }

    fp = fopen(PAYLOAD_DST, "wb");
    if (!fp)
    {
        LOG("Failed to open %s for writing", PAYLOAD_DST);
        return -1;
    }

    if (fwrite(start, 1, size, fp) != size)
    {
        LOG("Failed to write payload");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    chmod(PAYLOAD_DST, 0777);
    LOG("Payload installed successfully");
    return 0;
}
#endif

#define LOG_DIR  "/data/db-rebuilder"
#define LOG_PATH LOG_DIR "/log.txt"

int main(void)
{
    int ret = 0;

    if (mkdirs(LOG_PATH) != SUCCESS)
    {
        printf("Failed to create log directory\n");
        return 1;
    }

    if (log_init(LOG_PATH) != 0)
    {
        printf("Failed to open log file\n");
        return 1;
    }

    LOG("============================================================");
#ifdef BUILD_INSTALLER
    LOG("DB Rebuilder (Installer)");
#else
    LOG("DB Rebuilder");
#endif
    LOG("Build: %s %s", __DATE__, __TIME__);
    LOG("============================================================");

    LOG("Rebuilding app.db (%s)...", APP_DB_PATH);
    if (!appdb_rebuild(APP_DB_PATH))
    {
        LOG("app.db rebuild failed");
        ret = 1;
    }
    else
    {
        LOG("app.db rebuild completed");
    }

    LOG("Rebuilding addcont.db (%s)...", ADDCONT_DB_PATH);
    if (!addcont_dlc_rebuild(ADDCONT_DB_PATH))
    {
        LOG("addcont.db rebuild failed");
        ret = 1;
    }
    else
    {
        LOG("addcont.db rebuild completed");
    }

#ifdef BUILD_INSTALLER
    LOG("Installing payload...");
    if (install_payload() != 0)
    {
        LOG("Payload installation failed");
        ret = 1;
    }
#endif

    LOG("Done.");
    log_fini();
    return ret;
}
