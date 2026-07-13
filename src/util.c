// thanks bucanero: https://github.com/bucanero/apollo-ps4/blob/main/source/util.c

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static FILE* g_log_file = NULL;

int log_init(const char* path)
{
    if (mkdirs(path) != SUCCESS)
        return -1;

    g_log_file = fopen(path, "w");
    if (!g_log_file)
        return -1;

    setvbuf(g_log_file, NULL, _IOLBF, 0);
    LOG("Log started");
    return 0;
}

void log_fini(void)
{
    if (g_log_file)
    {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void LOG(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    if (!fmt)
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;

    if (g_log_file)
        fprintf(g_log_file, "%s\n", buf);

    printf("%s\n", buf);
    fflush(stdout);
}

int file_exists(const char* path)
{
    if (!path)
        return FAILED;

    if (access(path, F_OK) == 0)
        return SUCCESS;

    return FAILED;
}

int dir_exists(const char* path)
{
    struct stat sb;

    if (!path)
        return FAILED;

    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
        return SUCCESS;

    return FAILED;
}

int mkdirs(const char* dir)
{
    char path[512];
    char* ptr;

    if (!dir)
        return FAILED;

    snprintf(path, sizeof(path), "%s", dir);

    ptr = strrchr(path, '/');
    if (ptr)
        *ptr = 0;

    if (path[0] == '\0')
        return SUCCESS;

    ptr = path;
    if (*ptr == '/')
        ptr++;

    while (*ptr)
    {
        while (*ptr && *ptr != '/')
            ptr++;

        char last = *ptr;
        *ptr = 0;

        if (file_exists(path) == FAILED)
        {
            if (mkdir(path, 0777) < 0)
                return FAILED;
            chmod(path, 0777);
        }

        *ptr++ = last;
        if (last == 0)
            break;
    }

    return SUCCESS;
}

int read_buffer(const char* file_path, uint8_t** data, size_t* size)
{
    FILE* fp;
    long sz;
    uint8_t* buf;

    if (!file_path || !data || !size)
        return FAILED;

    fp = fopen(file_path, "rb");
    if (!fp)
        return FAILED;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return FAILED;
    }

    sz = ftell(fp);
    if (sz < 0)
    {
        fclose(fp);
        return FAILED;
    }

    rewind(fp);

    buf = (uint8_t*)malloc(sz);
    if (!buf)
    {
        fclose(fp);
        return FAILED;
    }

    if ((size_t)sz > 0 && fread(buf, 1, sz, fp) != (size_t)sz)
    {
        free(buf);
        fclose(fp);
        return FAILED;
    }

    fclose(fp);

    *data = buf;
    *size = (size_t)sz;
    return SUCCESS;
}

int write_buffer(const char* file_path, const uint8_t* data, size_t size)
{
    FILE* fp;

    if (!file_path || !data)
        return FAILED;

    if (mkdirs(file_path) != SUCCESS)
        return FAILED;

    fp = fopen(file_path, "wb");
    if (!fp)
        return FAILED;

    if (size > 0 && fwrite(data, 1, size, fp) != size)
    {
        fclose(fp);
        return FAILED;
    }

    fclose(fp);
    chmod(file_path, 0777);
    return SUCCESS;
}

int get_file_size(const char* file_path, uint64_t* size)
{
    struct stat st;

    if (!file_path || !size)
        return FAILED;

    if (stat(file_path, &st) < 0)
        return FAILED;

    *size = (uint64_t)st.st_size;
    return SUCCESS;
}

int copy_file(const char* input, const char* output)
{
    FILE *in, *out;
    size_t read, written;
    char* buf;
    int ret = FAILED;

    if (!input || !output)
        return FAILED;

    if (mkdirs(output) != SUCCESS)
        return FAILED;

    in = fopen(input, "rb");
    if (!in)
        return FAILED;

    out = fopen(output, "wb");
    if (!out)
    {
        fclose(in);
        return FAILED;
    }

    buf = (char*)malloc(0x20000);
    if (!buf)
    {
        fclose(in);
        fclose(out);
        return FAILED;
    }

    do
    {
        read = fread(buf, 1, 0x20000, in);
        written = fwrite(buf, 1, read, out);
    } while (read == written && read == 0x20000);

    if (read == written)
        ret = SUCCESS;

    free(buf);
    fclose(in);
    fclose(out);

    if (ret == SUCCESS)
        chmod(output, 0777);

    return ret;
}
