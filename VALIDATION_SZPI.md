# SZPI NES 0.1.0 验证记录

日期：2026-10-21（Asia/Shanghai）。

## 已完成

- 在独立目录克隆 Retro-Go，上游提交固定为
  `4ced120669750ca7228fd0414211430c1d923166`，本地分支 `codex/szpi-nes`。
- 使用本机 ESP-IDF 5.2.3、Xtensa GCC 13.2.0 编译 launcher、retro-core 和 Bootloader。
- `build-szpi.ps1` 最终成功，完整日志：`logs/build-szpi.log`（忽略的本机产物）。
- `test-szpi.ps1` 通过，使用 w64devkit GCC 15.1.0 运行真实 C 代码，覆盖双触点
  方向+A、释放、无触点、坏帧、坐标转换、斜向、相反方向抵消、边界和绘制缓冲哨兵。
- `tests/check_szpi_image.py` 通过：镜像总大小、Retro-Go footer CRC、分区表 MD5、
  分区不重叠、每个应用与打包文件逐字节相同、应用头目标 ESP32-S3。
- 检查两个应用生成的 sdkconfig：16 MB Flash、Octal PSRAM、240 MHz CPU。
- 排除原样保留的第三方 codec 目录后，板级改动的 `git diff --cached --check` 通过；
  codec 原始源码含部分行尾空格，本次没有改写第三方文件。实际 C 绘图代码渲染的
  控制布局已检查。

| 产物 | 大小（bytes） | 镜像内位置 / 分区 |
|---|---:|---|
| Bootloader | 21616 | 0x000000 |
| launcher.bin | 589568 | 0x010000，分区 2097152 bytes |
| retro-core.bin | 1116352 | 0x210000，分区 2097152 bytes |
| szpi-nes_0.1.0_szpi-esp32s3.img | 4260096 | 完整镜像，从 0x0 写入 |

完整镜像 SHA-256：

```text
2eb196e00bb08623303856e17fbd4faa80318cd0063767ef422b5fb2cd811704
```

镜像包含时间及 Git 构建元数据，重新构建后 SHA-256 可能不同。
IDF app description 沿用上游 Git 版本策略，当前为 `4ced120-dirty`；Retro-Go 应用
显示版本和完整镜像版本为 `0.1.0`。

上游构建存在部分未使用值、旧 API 相关警告；本次没有把成功构建描述为零警告。
首轮失败原因为 IDF 提前依赖扫描未发现 codec 依赖，已调整依赖声明，最终构建通过。

## 实物验收待完成

本次没有执行串口烧录、读写实体 TF 卡或启动商业游戏 ROM。
没有测得该板上的 FPS、音频质量、触摸延迟，也没有证明所有游戏 mapper 都兼容。

上板顺序：

1. 按 README_SZPI.md 写入完整镜像，观察 PSRAM 容量日志。
2. 确认 ST7789 方向、颜色、背光正常，触摸 M/O 和方向键位置正确。
3. FAT32 TF 卡中放 `/roms/nes/*.nes`，确认 SDMMC 1-bit 挂载和 NES 列表。
4. 测试方向+A、方向+B 同按与完全松开，检查无粘键。
5. 检查游戏声音、暂停/恢复、保存/读取、返回 launcher 及重启后的存档。

原 bootload OTA/AI 工程业务文件未因本次移植而修改。
