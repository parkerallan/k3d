#include <stdlib.h>

#include <KGL/gl.h>

#include "font.h"

static int font_uv_not_cached(Font *font, int index) {
    return font->texUv[index][0] == INVALID_UV;
}

static void font_draw_char(Font *font, int index, float x, float y, float w, float h) {
    int col = index / font->rowStride;
    int row = index % font->rowStride;

    if(index < 0 || index >= ASCII_TOTAL_CHAR) {
        return;
    }

    if(font_uv_not_cached(font, index)) {
        font->texUv[index][0] = ((row * font->charWidth)) / font->texWidth;
        font->texUv[index][1] = ((row * font->charWidth) + font->charWidth) / font->texWidth;
        font->texUv[index][2] = ((col * font->charHeight)) / font->texHeight;
        font->texUv[index][3] = ((col * font->charHeight) + font->charHeight) / font->texHeight;
    }

    glColor1ui(font->color);

    glTexCoord2f(font->texUv[index][0], font->texUv[index][3]);
    glKosVertex2f(x, y + h);

    glTexCoord2f(font->texUv[index][1], font->texUv[index][3]);
    glKosVertex2f(x + w, y + h);

    glTexCoord2f(font->texUv[index][1], font->texUv[index][2]);
    glKosVertex2f(x + w, y);

    glTexCoord2f(font->texUv[index][0], font->texUv[index][2]);
    glKosVertex2f(x, y);
}

Font *font_init(float texWidth, float texHeight,
                unsigned char rowStride, unsigned char colStride,
                uint32_t color) {
    Font *font = (Font *)malloc(sizeof(Font));
    unsigned short index;

    if(!font) {
        return NULL;
    }

    font->texWidth = texWidth;
    font->texHeight = texHeight;
    font->rowStride = rowStride;
    font->colStride = colStride;
    font->charWidth = texWidth / rowStride;
    font->charHeight = texHeight / colStride;
    font->color = color;

    for(index = 0; index < ASCII_TOTAL_CHAR; ++index) {
        font->texUv[index][0] = INVALID_UV;
    }

    return font;
}

void font_free(Font *font) {
    free(font);
}

void font_print_string(Font *font, const char *str, float xpos, float ypos,
                       float width, float height) {
    float x = xpos;
    int index = 0;

    while(str[index] != '\0' && str[index] != '\n') {
        font_draw_char(font, str[index] - ASCII_SYMBOL_OFFSET, x, ypos, width, height);
        x += width;
        ++index;
    }
}