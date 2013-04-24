#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct nitro_buffer_t {
    char *area;
    int alloc;
    int size;
} nitro_buffer_t;

nitro_buffer_t *nitro_buffer_new();
void nitro_buffer_append(nitro_buffer_t *buf, const char *s, int bytes);
char *nitro_buffer_data(nitro_buffer_t *buf, int *size);
char *nitro_buffer_prepare(nitro_buffer_t *buf, int *growth);
void nitro_buffer_extend(nitro_buffer_t *buf, int bytes);
void nitro_buffer_destroy(nitro_buffer_t *buf);

#endif /* BUFFER_H */
