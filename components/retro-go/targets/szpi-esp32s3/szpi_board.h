#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void szpi_platform_init(void);
uint32_t szpi_read_touch(void);
bool szpi_set_amplifier(bool enabled);
#ifdef __cplusplus
}
#endif
