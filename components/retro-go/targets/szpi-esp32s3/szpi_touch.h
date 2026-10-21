#pragma once
/* 纯数据接口不访问 I2C，便于在电脑上测试坐标边界、双点按键与绘制越界。 */
#include <stddef.h>
#include <stdint.h>

uint32_t szpi_touch_keys_at(int x, int y);
/* packet 从触摸寄存器 0x02 开始；返回值可直接与实体按键按位或合并。 */
uint32_t szpi_touch_decode(const uint8_t *packet, size_t length);
/* pixels_be 是可修改的 LCD 发送行缓存，元素字节顺序已经面向 SPI 传输。 */
void szpi_touch_overlay_row(uint16_t *pixels_be, int left, int y, int width);
