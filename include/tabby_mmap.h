#pragma once

// Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7, platformio/espressif32 6.12.0) ile
// Arduino-ESP32 3.3.12 (ESP-IDF 5.5, platformio/espressif32 8.0.0) ayni
// kaynaktan derlensin. IDF 5 su uc adi ekledi:
//   esp_partition_mmap_handle_t, ESP_PARTITION_MMAP_DATA, esp_partition_munmap
// IDF 4.4 karsiliklari spi_flash_mmap_handle_t, SPI_FLASH_MMAP_DATA,
// spi_flash_munmap. Harici -D eslemesi gerekmez.
// O esleme yalnizca 2.0.17 icindir; 8.0.0 uzerinde birakilirsa adlar bozulur.

#include <esp_idf_version.h>
#include <esp_partition.h>

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_spi_flash.h>
#ifndef esp_partition_mmap_handle_t
typedef spi_flash_mmap_handle_t esp_partition_mmap_handle_t;
#endif
#ifndef ESP_PARTITION_MMAP_DATA
#define ESP_PARTITION_MMAP_DATA SPI_FLASH_MMAP_DATA
#endif
#ifndef esp_partition_munmap
#define esp_partition_munmap spi_flash_munmap
#endif
#endif
