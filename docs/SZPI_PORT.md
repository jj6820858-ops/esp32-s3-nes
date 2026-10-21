# 实战派 NES 移植记录

## 来源和边界

- 模拟器、launcher、菜单、存档、系统切换：
  https://github.com/ducalex/retro-go ，提交 `4ced120669750ca7228fd0414211430c1d923166`。
  许可见根目录 COPYING 和各模拟器子目录的许可。
- 移植规范：https://github.com/ducalex/retro-go/blob/master/PORTING.md 。
- 支持的 IDF 版本及 Windows 构建方式：
  https://github.com/ducalex/retro-go/blob/master/BUILDING.md 。本机用 5.2.3。
- 官方板级 LCD 初始化与方向：
  https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html 。
- 官方触摸接入：
  https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lvgl.html 。
  FT6336 原始寄存器布局参照旧工程使用的 Espressif FT5x06 驱动 1.0.7；
  坐标变换参照 `esp_lcd_touch` 的 mirror-X 后 swap-XY 顺序。
- 官方 TF 卡 1-bit SDMMC 接法：
  https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/sd-card.html 。
- 官方引脚限制、用户键：
  https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/key.html 。
- IDF 5.2.3 SDMMC API：
  https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/api-reference/peripherals/sdmmc_host.html 。
- I2C API：
  https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32s3/api-reference/peripherals/i2c.html 。
- `components/esp_codec_dev` 为 Espressif 1.3.6 官方组件的本地副本，从现有组件缓存
  复制，保留 Apache-2.0 LICENSE、源码和组件清单。原组件指向 esp-adf 提交
  `1e31f0458b6cfe076a5b235afb7aa0112ead58cf` 的 `components/esp_codec_dev`。
  API：https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.6 。

没有从原 OTA/AI 工程复制业务源码。核对它的板级配置后，按 Retro-Go target 接口实现。
保留了上游使用的 IDF legacy I2C API，确保与本地 `esp_codec_dev` 1.3.6 的总线兼容。

## 增加的板级适配

`components/retro-go/targets/szpi-esp32s3/`：

- `config.h`：实际 GPIO、ST7789 命令、SPI mode 2、反相背光、SDMMC 及 BOOT。
- `sdkconfig`：16 MB Flash、Octal PSRAM 80 MHz、240 MHz CPU、UTF-8 FAT 文件名。
- `env.py`：仍使用上游的 launcher + retro-core 双应用镜像，两个 2 MB 应用分区。
- `szpi_board.c`：启动 I2C、PCA9557、LCD 片选、关闭摄像头、触摸读取及 PA 控制。
- `szpi_touch.c`：两个 FT6336 触点转换成 Retro-Go 按键；固定位置的半透明提示。

`drivers/audio/szpi.c` 将原有 `rg_audio_driver_t` 接口接到 ES8311，通过 IDF 新 I2S
驱动和 esp_codec_dev 输出立体声 PCM；硬件 DAC 为单声道，最终播放以本板为准。

## 对上游公共文件的改动

- `config.h` 注册 target；CMake 包含 target 源码与 codec 依赖。
- `rg_input.c` 合并触摸键并登记实际可用键。
- `rg_audio.c` 登记 ES8311 sink。
- `rg_display.c` 只在本 target 的传输 staging buffer 叠加触摸提示。
  原始 NES 帧和调色板不受修改；触摸提示不会写入游戏截图。
- 显示驱动增加可配置 SPI mode；SDMMC 对本板启用内部上拉。
- 启动阶段跳过 GPIO=-1 的 LCD 片选预设，由 PCA9557 执行实际片选。
- `rg_tool.py` 在 Windows 使用当前 Python、参数数组及 `shell=False` 调用 IDF，
  避免命中其他 Python 或错误解析带空格路径。

没有改动 Nofrendo 的 CPU/PPU/APU/mapper、ROM 解析、存档格式或游戏规则。
首次构建不打开网络功能。上游 retro-core 内的其他模拟器保持捆绑状态，未逐一验收。

## 验证记录

主机测试和完整编译的最终结果见 `VALIDATION_SZPI.md`。构建日志放在忽略的 `logs/`；
镜像及 build 产物不进入 Git。运行前需用户提供 TF 卡和 ROM。
