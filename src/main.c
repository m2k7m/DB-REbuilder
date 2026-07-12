#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "util.h"
#include "sqlite_db.h"

/* ── Notifications ── */
typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

void send_notification(const char* message)
{
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ── Constants ── */
#define APP_NAME        "DB-Rebuilder"
#define APP_COPYRIGHT   "(c) 4GAMER"
#define DATA_DIR        "/data/DB-Rebuilder"
#define LOG_PATH        DATA_DIR "/log.txt"

#ifdef BUILD_INSTALLER
#include "payload_elf.h"
#define PAYLOAD_DST "/data/payloads/db-rebuilder-" PAYLOAD_VERSION ".elf"

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

    LOG("===================================");
#ifdef BUILD_INSTALLER
    LOG("DB Rebuilder (Installer)");
#else
    LOG("DB Rebuilder");
#endif
    LOG("===================================");

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
    if (install_payload() != 0)
    {
        LOG("Payload installation failed");
        ret = 1;
    }
#endif

    LOG("Done.");
    log_fini();

    char done_msg[256];
    snprintf(done_msg, sizeof(done_msg), "%s v%s %s\n%s",
             APP_NAME, PAYLOAD_VERSION, APP_COPYRIGHT,
             (ret == 0) ? "Database rebuilt successfully." : "Database rebuilt with errors.");
    send_notification(done_msg);

    return ret;
}
