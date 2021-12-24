#include <string.h>

#include "esp_heap_caps.h"
#include "esp32_malloc.h"

#include "esp_log.h"

#define TAG "esp32_malloc"

inline void *esp32_malloc(size_t sz)
{
	//ESP_LOGI(TAG, "heap_caps_malloc %d bytes", sz);
	void* ret = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
	if (ret)
		return ret;
	else
		return malloc(sz);
}

inline void *esp32_realloc(void *ptr, size_t sz)
{
	//ESP_LOGI(TAG, "heap_caps_realloc %d bytes", sz);
	void* ret = heap_caps_realloc(ptr, sz, MALLOC_CAP_SPIRAM);
	if (ret)
		return ret;
	else
		return realloc(ptr, sz);	
}

inline void esp32_free(void *ptr)
{
	//ESP_LOGI(TAG, "heap_caps_free 0x%08x", ptr);
	heap_caps_free(ptr);
}

inline char *esp32_strdup(const char *str)
{
    if(str == 0)
        return 0;

    size_t l = strlen(str);
    if(l == 0)
        return 0;

    char *r = heap_caps_malloc(l+1, MALLOC_CAP_SPIRAM);
    if(r) {
        memcpy(r, str, l+1);
    } else {
        r = (char *)malloc(l+1);
        if(r)
            memcpy(r, str, l+1);        
    }
    return r;
}

inline void *esp32_memdup(uint8_t *data, size_t len)
{
    if(data == 0 || len == 0)
        return 0;

    char *r = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if(r) {
        memcpy(r, data, len);
    } else {
        r = (char *)malloc(len);
        if(r)
            memcpy(r, data, len);        
    }
    return r;    
}
