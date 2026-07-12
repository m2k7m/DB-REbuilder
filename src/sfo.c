#include "sfo.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PKG_MAGIC   0x544E437Fu
#define SFO_MAGIC   0x46535000u
#define SFO_VERSION 0x0101u

#define SWAP32(_v) \
    ((((uint32_t)(_v) & 0xFF000000) >> 24) | \
     (((uint32_t)(_v) & 0x00FF0000) >>  8) | \
     (((uint32_t)(_v) & 0x0000FF00) <<  8) | \
     (((uint32_t)(_v) & 0x000000FF) << 24))

#define ALIGN(_v, _a) (((_v) + ((_a) - 1)) & ~((_a) - 1))

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t key_table_offset;
    uint32_t data_table_offset;
    uint32_t num_entries;
} sfo_header_t;

typedef struct {
    uint16_t key_offset;
    uint16_t param_format;
    uint32_t param_length;
    uint32_t param_max_length;
    uint32_t data_offset;
} sfo_index_table_t;

typedef struct {
    char* key;
    uint16_t format;
    uint32_t length;
    uint32_t max_length;
    size_t actual_length;
    uint8_t* value;
} sfo_param_t;

typedef struct {
    uint32_t id;
    uint32_t filename_offset;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t offset;
    uint32_t size;
    uint64_t padding;
} pkg_table_entry_t;

struct sfo_context_s {
    sfo_param_t* params;
    int count;
    int capacity;
};

static int read_sfo_from_pkg(const char* pkg_path, uint8_t** sfo_buffer, size_t* sfo_size)
{
    FILE* file;
    uint32_t pkg_file_count;
    uint32_t pkg_table_offset;
    pkg_table_entry_t entry;

    file = fopen(pkg_path, "rb");
    if (!file)
        return -1;

    if (fread(&pkg_file_count, sizeof(uint32_t), 1, file) != 1)
    {
        fclose(file);
        return -1;
    }

    if (pkg_file_count != PKG_MAGIC)
    {
        fclose(file);
        return -1;
    }

    if (fseek(file, 0x00C, SEEK_SET) != 0 ||
        fread(&pkg_file_count, sizeof(uint32_t), 1, file) != 1 ||
        fseek(file, 0x018, SEEK_SET) != 0 ||
        fread(&pkg_table_offset, sizeof(uint32_t), 1, file) != 1)
    {
        fclose(file);
        return -1;
    }

    pkg_file_count = SWAP32(pkg_file_count);
    pkg_table_offset = SWAP32(pkg_table_offset);

    if (fseek(file, pkg_table_offset, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }

    for (uint32_t i = 0; i < pkg_file_count; i++)
    {
        if (fread(&entry, sizeof(entry), 1, file) != 1)
            break;

        if (entry.id == 1048576)
        {
            *sfo_size = SWAP32(entry.size);
            *sfo_buffer = (uint8_t*)malloc(*sfo_size);
            if (!*sfo_buffer)
            {
                fclose(file);
                return -1;
            }

            if (fseek(file, SWAP32(entry.offset), SEEK_SET) != 0 ||
                fread(*sfo_buffer, *sfo_size, 1, file) != 1)
            {
                free(*sfo_buffer);
                fclose(file);
                return -1;
            }

            fclose(file);
            return 0;
        }
    }

    LOG("Could not find param.sfo inside PKG %s", pkg_path);
    fclose(file);
    return -1;
}

sfo_context_t* sfo_alloc(void)
{
    sfo_context_t* ctx = (sfo_context_t*)calloc(1, sizeof(sfo_context_t));
    if (!ctx)
        return NULL;

    ctx->capacity = 64;
    ctx->params = (sfo_param_t*)calloc(ctx->capacity, sizeof(sfo_param_t));
    if (!ctx->params)
    {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void sfo_free(sfo_context_t* ctx)
{
    if (!ctx)
        return;

    if (ctx->params)
    {
        for (int i = 0; i < ctx->count; i++)
        {
            free(ctx->params[i].key);
            free(ctx->params[i].value);
        }
        free(ctx->params);
    }

    free(ctx);
}

int sfo_read(sfo_context_t* ctx, const char* file_path)
{
    uint8_t* sfo = NULL;
    size_t sfo_size = 0;
    sfo_header_t* header;
    int ret = -1;

    if (!ctx || !file_path)
        return -1;

    size_t len = strlen(file_path);
    if (len > 4 && strcasecmp(file_path + len - 4, ".pkg") == 0)
    {
        if (read_sfo_from_pkg(file_path, &sfo, &sfo_size) != 0)
            return -1;
    }
    else
    {
        if (read_buffer(file_path, &sfo, &sfo_size) != SUCCESS)
            return -1;
    }

    if (sfo_size < sizeof(sfo_header_t))
        goto error;

    header = (sfo_header_t*)sfo;
    if (header->magic != SFO_MAGIC)
        goto error;

    for (uint32_t i = 0; i < header->num_entries; i++)
    {
        sfo_index_table_t* idx = (sfo_index_table_t*)(sfo + sizeof(sfo_header_t) + i * sizeof(sfo_index_table_t));
        sfo_param_t* param;

        if (ctx->count >= ctx->capacity)
        {
            int new_cap = ctx->capacity * 2;
            sfo_param_t* new_params = (sfo_param_t*)realloc(ctx->params, new_cap * sizeof(sfo_param_t));
            if (!new_params)
                goto error;
            ctx->params = new_params;
            ctx->capacity = new_cap;
        }

        param = &ctx->params[ctx->count];
        param->key = strdup((char*)(sfo + header->key_table_offset + idx->key_offset));
        param->format = idx->param_format;
        param->length = idx->param_length;
        param->max_length = idx->param_max_length;
        param->actual_length = idx->param_max_length;
        param->value = (uint8_t*)malloc(param->actual_length);
        if (!param->value)
            goto error;

        memcpy(param->value, sfo + header->data_table_offset + idx->data_offset, param->actual_length);
        ctx->count++;
    }

    ret = 0;

error:
    free(sfo);
    return ret;
}

uint8_t* sfo_get_param_value(sfo_context_t* ctx, const char* key)
{
    if (!ctx || !key)
        return NULL;

    for (int i = 0; i < ctx->count; i++)
    {
        if (ctx->params[i].key && strcmp(ctx->params[i].key, key) == 0)
            return ctx->params[i].value;
    }

    return NULL;
}
