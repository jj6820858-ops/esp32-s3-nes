#pragma once
#include <stddef.h>
#include <stdint.h>

uint32_t szpi_touch_keys_at(int x, int y);
uint32_t szpi_touch_decode(const uint8_t *packet, size_t length);
void szpi_touch_overlay_row(uint16_t *pixels_be, int left, int y, int width);
