/*
   Minimal textured font helper for KGL overlays.
*/

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define ASCII_SYMBOL_OFFSET 32
#define ASCII_TOTAL_CHAR (128 - ASCII_SYMBOL_OFFSET)
#define INVALID_UV -1.0f

typedef struct {
    float texWidth;
    float texHeight;
    float charWidth;
    float charHeight;
    unsigned char rowStride;
    unsigned char colStride;
    uint32_t color;
    float texUv[ASCII_TOTAL_CHAR][4];
} Font;

Font *font_init(float texWidth, float texHeight,
                unsigned char rowStride, unsigned char colStride,
                uint32_t color);

void font_free(Font *font);

void font_print_string(Font *font, const char *str, float xpos, float ypos,
                       float width, float height);

#endif