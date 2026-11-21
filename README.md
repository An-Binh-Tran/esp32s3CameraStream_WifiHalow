# XIAO ESP32S3 Sense — Flash / PSRAM Config (ESP-IDF)

Board: Seeed Studio XIAO ESP32S3 Sense
Module: ESP32-S3R8 — 8 MB flash (quad), 8 MB PSRAM (octal)

## Setup Rules 
idf.py set-target esp32s3 
idf.py menuconfig 
Flash SPI mode              → QIO
Flash SPI speed             → 80 MHz
Flash size                  → 8 MB

Component config → Hardware Settings → ESP PSRAM
  [*] Support for external, SPI-connected RAM
      SPI RAM config
        Mode of SPI RAM chip  → Octal Mode PSRAM
        Set RAM clock speed   → 80 MHz

## OR check sdkconfig file directly 
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y