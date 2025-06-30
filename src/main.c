//! @file

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "app_version.h"
#include "memfault/components.h"
#include "memfault/ports/zephyr/http.h"

#if defined(CONFIG_FILE_SYSTEM)
  #include "fs_log_init.h"
#endif

LOG_MODULE_REGISTER(mflt_app, LOG_LEVEL_DBG);

const char *memfault_zephyr_get_device_id(void) {
  uint8_t dev_id[16] = {0};
  static char dev_id_str[sizeof(dev_id) * 2 + 1];

  // Check if the device id has already been initialized
  if (dev_id_str[0]) {
    return dev_id_str;
  }

  // Obtain the device id
  ssize_t length = hwinfo_get_device_id(dev_id, sizeof(dev_id));

  // Render the obtained serial number in hexadecimal representation
  for (size_t i = 0; i < length; i++) {
    (void)snprintf(&dev_id_str[i * 2], sizeof(dev_id_str), "%02x", dev_id[i]);
  }

  return dev_id_str;
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info) {
  *info = (sMemfaultDeviceInfo){
    .device_serial = memfault_zephyr_get_device_id(),
    .software_type = "app",
    .software_version = APP_VERSION_STRING "+" ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1,
    .hardware_version = CONFIG_BOARD,
  };
}

//! Test command to fs log extraction
#include "memfault/ports/zephyr/memfault_fs_log_export.h"

static int cmd_test_fs_log_extraction(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  memfault_fs_log_trigger_export();
  shell_print(sh, "done");

  return 0;
}

SHELL_CMD_REGISTER(mflt_fs_log_extract, NULL, "Test command to trigger file system log extraction",
                   cmd_test_fs_log_extraction);

int main(void) {
  printk("\n" MEMFAULT_BANNER_COLORIZED);

  LOG_INF("Memfault Demo App! Board %s\n", CONFIG_BOARD);

  memfault_device_info_dump();
  memfault_zephyr_port_install_root_certs();

#if defined(CONFIG_FILE_SYSTEM)
  (void)fs_log_init();
#endif

  return 0;
}
