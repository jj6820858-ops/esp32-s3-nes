# SZPI NES 0.1.0 实机验证记录

日期：2026-10-21（Asia/Shanghai）。独立工程基于 Retro-Go 上游提交
`4ced120669750ca7228fd0414211430c1d923166`，分支 `codex/szpi-nes`。

## 已完成的实机验证

- COM5（CH340K/UART0）烧录，460800 波特率；esptool 校验通过。
- ESP32-S3 rev 0.2，16 MB Flash，8 MB Octal PSRAM / 80 MHz，PSRAM 内存测试通过。
- TF 卡以 SDMMC 1-bit / 20 MHz 初始化，读出 3 个已有主分区，选择第 3 个 FAT32
  分区：起始 LBA 2097152，长度 13631487 扇区，约 6655 MiB。
- 在选定分区创建临时文件，写入、同步、重开、读取比较并删除，结果 PASS。
  MBR 与 3 个分区启动扇区写入前后指纹一致（4 扇区）。这不是全卡旧文件逐字节校验；
  其他分区的写隔离由每次磁盘读写的范围检查保证。没有格式化或重新分区。
- 自编 `SZPI-Controls.nes` 已写入 TF，Mapper 0，PRG 16 KiB、CHR 8 KiB。
- 用户实物确认：菜单、虚拟按键、游戏画面、触摸、A 键声音均正常。
- 用户确认 440 Hz / 2 秒直接 PCM 测试音可听到；功放读回 0x06（PA 开，LCD CS 低），
  DAC 静音寄存器 0x31 读回 0x00。修正 I2S 启停状态后不再出现重复 disable 报错。
- 游戏串口统计约 60 个模拟帧/秒、30 个显示提交/秒，默认 frameskip=1；这不是
  示波器测量的屏幕刷新率，也不代表所有 ROM 都达到相同速度。
- 通过原菜单保存存档 0，日志 `state_save: Game saved!`；再加载，日志
  `state_load: Game restored`。文件位于 `/sd/retro-go/saves/nes/`。
- NES → launcher → NES 连续两轮通过，无 LCD/I2C 退出断言；最终停留在测试游戏。

## 黑屏及退出断言的定位

已从备份完整恢复原 16 MB Flash 做对照。用户断电重上电后确认原 OTA/语音界面
正常，随后重新烧录 NES 工程。原业务源码未修改。

1. 原移植在 SPI 初始化前拉低 LCD CS，可能使引脚复用/空闲电平变化被屏幕采样。
   现在启动保持 CS=1，SPI 先完成一次事务建立 mode 2 空闲电平，然后选中屏幕、
   发送有效软件复位，再通过 IDF ST7789 驱动初始化。用户确认黑屏消失。
2. 退出时原流程先关闭 I2C，再关闭显示。本板释放 CS 需要写 PCA9557，故出现
   `szpi_set_lcd_selected(false)` / `LCD chip deselect failed`。
   已从 TF 的 `/crash.log` 读回确认：`Panic context: lcd_deinit`，前面有
   `I2C driver terminated`、`init=0`。现改为显示退出完成后再关闭 I2C，并完成
   游戏与菜单切换回归。TF 中旧 crash.log 保留，文件存在不代表新固件仍然报错。

## VS Code 与构建

请打开 `SZPI-NES.code-workspace`。IDF 工程目录为 `retro-core`，仓库根目录不是
IDF 工程。工作区设置 COM5、UART、115200 监视、完整镜像打包命令。

- ESP-IDF 5.2.3 / Xtensa GCC 13.2.0 完整编译通过。
- 实际执行 `idf.py -C retro-core build` 成功。
- 实际执行 `idf.py -C retro-core -p COM5 -b 460800 flash` 成功，校验通过。
- `flasher_args.json` 普通 flash 仅写完整镜像到 0x0；app-flash 对应 0x210000，
  不会把占位表或模拟器写入 launcher 地址。已通过自动元数据检查。
- VS Code 原生按钮没有通过 GUI 自动点击；上述验证覆盖其对应工程目录、构建及
  烧录命令。工作区使用本机现有 IDF 安装路径，换电脑需要修改路径。

| 产物 | 大小（bytes） | 位置 |
|---|---:|---|
| Bootloader | 21616 | 0x000000 |
| launcher.bin | 620240 | 0x010000，2 MiB 分区 |
| retro-core.bin | 1147664 | 0x210000，2 MiB 分区 |
| szpi-nes_0.1.0_szpi-esp32s3.img | 4260096 | 从 0x0 写入 |

本次烧录镜像 SHA-256：

```text
94694b1614eec2fab4beedc2b0ec2b29488729675c32549cf625fd563df4c8be
```

构建包含时间及 Git 元数据，重新构建后校验值可能变化。Retro-Go 显示版本 0.1.0，
IDF app description 沿用 Git 版本策略，本次构建为 `3c4aa6b-dirty`。

## 主机检查和证据

- `test-szpi.ps1`：真实 C 触摸解码/叠加层、MBR/BPB/分区范围检查、实际 Nofrendo
  核心执行测试 ROM 的移动、非零音频、B 键调色板、帧图案检查，均通过。
- `tests/check_szpi_image.py`：镜像 CRC、分区表 MD5、不重叠、应用逐字节一致、
  ESP32-S3 目标及 PSRAM 配置、VS Code 工程目录和烧录地址，均通过。
- `tools/test_board_menu.py`：保存、加载及两轮应用切换实机回归。
- 日志均在忽略的 `logs/` 中：`tf-crash-readback.log`、`board-audio-clock.log`、
  `board-speaker-tone-repeat.log`、`board-game-audio-final.log`、
  `board-save-load-roundtrip.log`、`flash-lcd-cs-idf.log`、`test-final.log`。
- 原 16 MB 固件备份位于忽略的 `backups/before-nes-20261021.bin`，SHA-256：
  `14650427d1024c527258c02c85aaf5afe1e83cb4ad41bf4b6490445f3e38d6ff`。
  备份可能含原设备配置，不进入 Git。

## 验证范围

尚未测试商业游戏、其他 mapper、其他模拟器、长时间运行、耳机输出或音频频响。
没有测量触摸延迟或 SPI/I2S 实际波形。主机双触点解码测试通过，但不同游戏的
复杂双点组合仍需实际游玩确认。代码沿用部分上游旧 API，保留其相关编译警告。
