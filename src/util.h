#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>
#include <stddef.h>

#define SUCCESS 0
#define FAILED -1

int log_init(const char* path);
void log_fini(void);
void LOG(const char* fmt, ...);

int file_exists(const char* path);
int dir_exists(const char* path);
int mkdirs(const char* dir);
int read_buffer(const char* file_path, uint8_t** data, size_t* size);
int write_buffer(const char* file_path, const uint8_t* data, size_t size);
int get_file_size(const char* file_path, uint64_t* size);
int copy_file(const char* input, const char* output);

#endif /* !_UTIL_H_ */
