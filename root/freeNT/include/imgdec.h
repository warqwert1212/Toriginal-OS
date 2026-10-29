/* imgdec.h */
#ifndef IMGDEC_H
#define IMGDEC_H

#include <stdint.h>
#include <stddef.h>
#include "png.h"

int image_decode(const uint8_t *data, size_t len, png_image_t *out);
int image_has_supported_suffix(const char *name, uint8_t name_len);

#endif
