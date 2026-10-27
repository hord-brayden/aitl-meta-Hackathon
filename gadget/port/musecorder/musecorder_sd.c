// See musecorder_sd.h.
#include "musecorder_sd.h"
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static const char *TAG = "musecorder";
static sdmmc_card_t *s_card;
static int s_users;

bool mc_sd_mount(void) {
    if (s_card) { s_users++; return true; }
    spi_bus_config_t bus = {
        .mosi_io_num = 23, .miso_io_num = 19, .sclk_io_num = 18,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    esp_err_t e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) { ESP_LOGW(TAG, "sd: spi bus %s", esp_err_to_name(e)); return false; }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = 5;
    dev.host_id = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t cfg = { .format_if_mount_failed = false, .max_files = 2, .allocation_unit_size = 0 };
    e = esp_vfs_fat_sdspi_mount("/sd", &host, &dev, &cfg, &s_card);
    if (e != ESP_OK) { ESP_LOGW(TAG, "sd: mount %s", esp_err_to_name(e)); spi_bus_free(SPI3_HOST); s_card = NULL; return false; }
    mkdir("/sd/mc", 0775);
    s_users = 1;
    return true;
}

void mc_sd_unmount(void) {
    if (!s_card) return;
    if (--s_users > 0) return;
    esp_vfs_fat_sdcard_unmount("/sd", s_card);
    s_card = NULL;
    spi_bus_free(SPI3_HOST);
}

bool mc_sd_mounted(void) { return s_card != NULL; }
