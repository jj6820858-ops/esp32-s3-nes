# SPDX-License-Identifier: GPL-2.0-or-later
# 在 project() 之后接入。Retro-Go 原本由 mkfw.py 打包，根目录 partitions.csv
# 只是占位表；不能把普通 IDF 的默认 app 地址当成 retro-core 的真实地址。
if(RG_BUILD_TARGET STREQUAL "RG_TARGET_SZPI_ESP32S3")
    idf_build_get_property(szpi_build_dir BUILD_DIR)
    get_filename_component(szpi_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(szpi_image "${szpi_root}/szpi-nes_0.1.0_szpi-esp32s3.img")
    file(RELATIVE_PATH szpi_image_relative "${szpi_build_dir}" "${szpi_image}")

    # VS Code 普通烧录按钮使用完整镜像，包含 launcher 和 retro-core。
    # 镜像由工作区的 postBuildTask 更新；未构建时 esptool 会明确报告缺文件。
    set_property(TARGET flash PROPERTY IMAGES "0x0 ${szpi_image_relative}")
    set_property(TARGET flash PROPERTY FLASH_FILE "\"0x0\" : \"${szpi_image_relative}\"")
    set_property(TARGET flash PROPERTY FLASH_ENTRY
        "\"complete-image\" : {\"offset\":\"0x0\",\"file\":\"${szpi_image_relative}\",\"encrypted\":\"false\"}")

    # 单独 app-flash 也必须定位到实际模拟器分区，不能覆盖 0x10000 的菜单。
    foreach(szpi_property IMAGES FLASH_FILE FLASH_ENTRY)
        set_property(TARGET app-flash PROPERTY ${szpi_property} "")
    endforeach()
    esptool_py_flash_target_image(app-flash app 0x210000 "${szpi_build_dir}/retro-core.bin")
    get_property(szpi_app_entry TARGET app-flash PROPERTY FLASH_ENTRY)
    set_property(TARGET flash APPEND PROPERTY FLASH_ENTRY "${szpi_app_entry}")
endif()
