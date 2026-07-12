#ifndef _SFO_H_
#define _SFO_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sfo_context_s sfo_context_t;

sfo_context_t* sfo_alloc(void);
void sfo_free(sfo_context_t* context);

int sfo_read(sfo_context_t* context, const char* file_path);
uint8_t* sfo_get_param_value(sfo_context_t* context, const char* key);

#ifdef __cplusplus
}
#endif

#endif /* !_SFO_H_ */
