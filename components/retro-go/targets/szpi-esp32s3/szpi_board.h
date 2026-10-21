#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void szpi_platform_init(void);
uint32_t szpi_read_touch(void);
bool szpi_set_amplifier(bool enabled);
bool szpi_set_lcd_selected(bool selected);
uint32_t szpi_console_read_keys(void);
void szpi_console_poll(void);
#ifdef __cplusplus
}
#endif
