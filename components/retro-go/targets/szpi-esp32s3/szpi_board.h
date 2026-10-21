#pragma once
/* 板级接口供公共驱动调用；触摸和串口都返回 RG_KEY_* 的组合位掩码。 */
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void szpi_platform_init(void);
/* 读取一次当前触摸状态；无触点或读取失败时返回 0。 */
uint32_t szpi_read_touch(void);
/* 通过同一 PCA9557 输出端口控制功放与 LCD，调用时 I2C 必须仍已初始化。 */
bool szpi_set_amplifier(bool enabled);
bool szpi_set_lcd_selected(bool selected);
/* 串口注入临时按键并处理调试命令，便于没有实体游戏手柄时验证输入链路。 */
uint32_t szpi_console_read_keys(void);
void szpi_console_poll(void);
#ifdef __cplusplus
}
#endif
