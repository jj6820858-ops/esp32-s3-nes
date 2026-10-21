// SPDX-License-Identifier: GPL-2.0-or-later
#include "szpi_touch.h"
#include "rg_input.h"

typedef struct { int x, y, w, h; uint32_t key; const char *label; } touch_button_t;
static const touch_button_t buttons[] = {
    /* 坐标单位是横屏像素；触摸命中这些区域后生成 Retro-Go 原有按键。 */
    {40,144,32,32,RG_KEY_UP,"U"}, {8,176,32,32,RG_KEY_LEFT,"L"},
    {72,176,32,32,RG_KEY_RIGHT,"R"}, {40,208,32,32,RG_KEY_DOWN,"D"},
    {216,192,44,40,RG_KEY_B,"B"}, {268,160,44,40,RG_KEY_A,"A"},
    {108,216,44,24,RG_KEY_SELECT,"SEL"}, {160,216,48,24,RG_KEY_START,"START"},
    {0,0,36,24,RG_KEY_MENU,"M"}, {284,0,36,24,RG_KEY_OPTION,"O"},
};

uint32_t szpi_touch_keys_at(int x, int y)
{
    /* 九宫格方向盘：四角可以同时产生两个方向，中间是无动作区。 */
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return 0;
    if (x >= 8 && x < 104 && y >= 144) {
        uint32_t key = 0;
        if (x < 40) key |= RG_KEY_LEFT;
        else if (x >= 72) key |= RG_KEY_RIGHT;
        if (y < 176) key |= RG_KEY_UP;
        else if (y >= 208) key |= RG_KEY_DOWN;
        return key;
    }
    for (size_t i = 4; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        const touch_button_t *b = &buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return b->key;
    }
    return 0;
}

uint32_t szpi_touch_decode(const uint8_t *packet, size_t length)
{
    if (!packet || length < 13 || (packet[0] & 0x0f) > 2) return 0;
    uint32_t keys = 0;
    for (unsigned i = 0; i < (packet[0] & 0x0f); ++i) {
        const uint8_t *p = packet + 1 + 6 * i;
        unsigned event = p[0] >> 6;
        if (event == 1 || event == 3 || (p[2] >> 4) == 0x0f) continue;
        int raw_x = ((p[0] & 0x0f) << 8) | p[1];
        int raw_y = ((p[2] & 0x0f) << 8) | p[3];
        if (raw_x > 240 || raw_y >= 320) continue;
        // 与原板级驱动保持一致：原始 X 镜像后再交换 X/Y，得到横屏坐标。
        int y = 240 - raw_x;
        if (y == 240) y = 239;
        keys |= szpi_touch_keys_at(raw_y, y);
    }
    if ((keys & (RG_KEY_LEFT | RG_KEY_RIGHT)) == (RG_KEY_LEFT | RG_KEY_RIGHT))
        keys &= ~(RG_KEY_LEFT | RG_KEY_RIGHT);
    if ((keys & (RG_KEY_UP | RG_KEY_DOWN)) == (RG_KEY_UP | RG_KEY_DOWN))
        keys &= ~(RG_KEY_UP | RG_KEY_DOWN);
    return keys;
}

// 触摸按键仅叠加到 LCD 发送缓存，不能修改模拟器原始画面和调色板。
// 因此游戏存档截图中不会混入虚拟手柄。
static unsigned glyph(char c)
{
    switch (c) {
    case 'A': return 0x2BED; case 'B': return 0x6BAE;
    case 'D': return 0x6B6E; case 'E': return 0x79A7;
    case 'L': return 0x4927; case 'M': return 0x7FED;
    case 'O': return 0x7B6F; case 'R': return 0x7BAD;
    case 'S': return 0x79CF; case 'T': return 0x7492;
    case 'U': return 0x5B6F; default: return 0;
    }
}

void szpi_touch_overlay_row(uint16_t *pixels_be, int left, int y, int width)
{
    for (size_t n = 0; n < sizeof(buttons) / sizeof(buttons[0]); ++n) {
        const touch_button_t *b = &buttons[n];
        if (y < b->y || y >= b->y + b->h) continue;
        int first = b->x > left ? b->x : left;
        int last = b->x + b->w < left + width ? b->x + b->w : left + width;
        int len = 0;
        while (b->label[len]) ++len;
        int text_x = b->x + (b->w - (len * 8 - 2)) / 2;
        int text_y = b->y + (b->h - 10) / 2;
        for (int x = first; x < last; ++x) {
            uint16_t be = pixels_be[x - left], le = (be >> 8) | (be << 8);
            uint16_t color = (le & 0xF7DE) >> 1;
            if (x == b->x || x == b->x + b->w - 1 || y == b->y || y == b->y + b->h - 1)
                color = 0xBDF7;
            int tx = x - text_x, ty = y - text_y;
            if (tx >= 0 && tx < len * 8 && ty >= 0 && ty < 10 && tx % 8 < 6) {
                unsigned bits = glyph(b->label[tx / 8]);
                if ((bits >> (14 - (ty / 2) * 3 - (tx % 8) / 2)) & 1) color = 0xFFFF;
            }
            pixels_be[x - left] = (color >> 8) | (color << 8);
        }
    }
}
