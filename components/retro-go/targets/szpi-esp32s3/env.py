# 供 rg_tool.py 读取的板级打包参数；容量单位为字节，不是 TF 卡分区大小。
IDF_TARGET = "esp32s3"
FW_FORMAT = "none"
DEFAULT_APPS = "launcher retro-core"
PROJECT_NAME = "SZPI-NES"
PROJECT_VER = "0.1.0"
# 保留 launcher + retro-core 多应用布局，分别预留 2 MiB Flash 应用空间。
PROJECT_APPS['launcher'][2] = 2 * 1024 * 1024
PROJECT_APPS['retro-core'][2] = 2 * 1024 * 1024
