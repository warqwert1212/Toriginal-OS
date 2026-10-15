#ifndef _IMAGE_LOADER_H
#define _IMAGE_LOADER_H

#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    int32_t   width;
    int32_t   height;
} image_t;

int  image_load(const char *path, image_t *out);
int  image_downscale(const image_t *src, int32_t target_w, int32_t target_h, image_t *out);
void image_free(image_t *img);

#endif
