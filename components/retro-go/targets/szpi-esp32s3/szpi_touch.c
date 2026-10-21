// SPDX-License-Identifier: GPL-2.0-or-later
/* 触摸链路：FT6336/FT5x06 数据包 → 横屏坐标 → 按键位掩码。
 * 绘制链路只在发送到 LCD 的 RGB565 行缓存上画按键，两条链路共享 buttons 布局。 */
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
    /* 区域使用左闭右开边界，相邻矩形的边界像素不会同时命中两次。 */
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
        /* 前四项用于方向标识绘制；方向命中已由上面的九宫格统一处理。 */
        const touch_button_t *b = &buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return b->key;
    }
    return 0;
}

uint32_t szpi_touch_decode(const uint8_t *packet, size_t length)
{
    /* packet[0] 低 4 位是触点数；完整双点报告需要 13 字节。
     * 每个触点的前 4 字节含事件、12 位 X、触点 ID 与 12 位 Y，后 2 字节不参与按键映射。 */
    if (!packet || length < 13 || (packet[0] & 0x0f) > 2) return 0;
    uint32_t keys = 0;
    for (unsigned i = 0; i < (packet[0] & 0x0f); ++i) {
        const uint8_t *p = packet + 1 + 6 * i;
        unsigned event = p[0] >> 6;
        /* 事件 0=按下、1=抬起、2=接触、3=保留；ID=0xF 也视为无效触点。
         * 不保留上次坐标或上次键值，避免把抬起报告误认为仍在按键。 */
        if (event == 1 || event == 3 || (p[2] >> 4) == 0x0f) continue;
        int raw_x = ((p[0] & 0x0f) << 8) | p[1];
        int raw_y = ((p[2] & 0x0f) << 8) | p[3];
        if (raw_x > 240 || raw_y >= 320) continue;
        // 与原板级驱动保持一致：原始 X 镜像后再交换 X/Y，得到横屏坐标。
        int y = 240 - raw_x;
        if (y == 240) y = 239;
        keys |= szpi_touch_keys_at(raw_y, y);
    }
    /* 位或使两个触点可组合为“方向 + A/B”等按键；左右或上下同时命中时
     * 清除该对方向，保留其余按键，避免向模拟器输入物理手柄难以产生的对向状态。 */
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
    /* 每字形用 15 位存 3×5 点阵，绘制时放大到 6×10，节省独立图片资源。 */
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
    /* pixels_be 仅覆盖屏幕的 [left, left+width) 这一行片段，不能按整屏宽度写入。
     * 先求按键矩形与片段的交集，索引用 x-left，保证局部刷新也不会越界。 */
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
            /* LCD 传输缓存已经按高字节先发排列；还原成 RGB565 数值后再计算颜色。
             * 0xF7DE 清掉各分量右移时会串入相邻分量的位，使背景各分量减半。 */
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
