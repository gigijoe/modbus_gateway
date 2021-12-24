#ifndef _ESP32_MALLOC_H
#define _ESP32_MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

void *esp32_malloc(size_t sz);
void *esp32_realloc(void *ptr, size_t sz);
void esp32_free(void *ptr);

char *esp32_strdup(const char *str);
void *esp32_memdup(uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
