IDF_TARGET = "esp32s3"
FW_FORMAT = "none"
DEFAULT_APPS = "launcher retro-core"
PROJECT_NAME = "SZPI-NES"
PROJECT_VER = "0.1.0"
# Preserve Retro-Go's multi-application layout, with more space on 16MB Flash.
PROJECT_APPS['launcher'][2] = 2 * 1024 * 1024
PROJECT_APPS['retro-core'][2] = 2 * 1024 * 1024
