// When testing SD and SPI modes, keep in mind that once the card has been
// initialized in SPI mode, it can not be reinitialized in SD mode without
// toggling power to the card.

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "sdmmc.h"

static const char *TAG = "sdmmc";

void initialize_sdmmc()
{
    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    //host.slot = VSPI_HOST;
    //host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    //host.max_freq_khz = SDMMC_FREQ_PROBING;

    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    //slot_config.dma_channel = 0;

    slot_config.gpio_miso = (gpio_num_t)(CONFIG_SPISD_MISO_GPIO);
    slot_config.gpio_mosi = (gpio_num_t)(CONFIG_SPISD_MOSI_GPIO);
    slot_config.gpio_sck  = (gpio_num_t)(CONFIG_SPISD_CLK_GPIO);
    slot_config.gpio_cs   = (gpio_num_t)(CONFIG_SPISD_CS_GPIO);
    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    //card->host.command_timeout_ms = 2000;
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
}
