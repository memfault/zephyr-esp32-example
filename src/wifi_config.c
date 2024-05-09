#include "wifi_config.h"

// #include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
// #include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(wifi_config);

static struct nvs_fs fs;

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define SSID_ID 1
#define PASSWORD_ID 2

int wifi_config_get(struct wifi_config *config) {
  int rc;
  if (!fs.ready) {
    LOG_ERR("Flash not ready");
    return -1;
  }

  rc = nvs_read(&fs, SSID_ID, config->ssid, sizeof(config->ssid));
  if (rc < 0) {
    LOG_ERR("Failed to read SSID");
    return -1;
  }

  rc = nvs_read(&fs, PASSWORD_ID, config->password, sizeof(config->password));
  if (rc < 0) {
    LOG_ERR("Failed to read password");
    return -1;
  }

  return 0;
}

static int cmd_wifi_config(const struct shell *sh, size_t argc, char *argv[]) {
  const char *ssid = argv[1];
  const char *password = argv[2];

  if (argc != 3) {
    struct wifi_config config;
    int rc = wifi_config_get(&config);
    if (rc < 0) {
      LOG_ERR("Failed to get wifi config");
      return -1;
    }
    shell_print(sh, "SSID: %s, Password: %s", config.ssid, config.password);
    return 0;
  }

  shell_print(sh, "Setting SSID: %s, Password: %s", ssid, password);

  if (!fs.ready) {
    LOG_ERR("Flash not ready");
    return -1;
  }

  int rc = nvs_write(&fs, SSID_ID, ssid, strlen(ssid));
  if (rc < 0) {
    LOG_ERR("Failed to write SSID");
    return -1;
  }

  rc = nvs_write(&fs, PASSWORD_ID, password, strlen(password));
  if (rc < 0) {
    LOG_ERR("Failed to write password");
    return -1;
  }

  return 0;
}

SHELL_CMD_ARG_REGISTER(wifi_config, NULL,
                       "Set wifi SSID + password\n"
                       "<SSID> <PASSWORD>\n",
                       cmd_wifi_config, 0, 2);

static int wifi_config_init(void) {
  int rc;
  struct flash_pages_info info;

  LOG_INF("Flash Init start");

  /* define the nvs file system by settings with:
   *	sector_size equal to the pagesize,
   *	3 sectors
   *	starting at NVS_PARTITION_OFFSET
   */
  fs.flash_device = NVS_PARTITION_DEVICE;
  if (!device_is_ready(fs.flash_device)) {
    LOG_ERR("Flash device %s is not ready", fs.flash_device->name);
    return 1;
  }
  fs.offset = NVS_PARTITION_OFFSET;
  rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
  if (rc) {
    LOG_ERR("Unable to get page info");
    return 1;
  }
  fs.sector_size = info.size;
  fs.sector_count = 3U;

  rc = nvs_mount(&fs);
  if (rc) {
    LOG_ERR("Flash Init failed");
    return 1;
  }
  LOG_INF("Flash Init done");
  return 0;
}

SYS_INIT(wifi_config_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
