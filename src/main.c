//! @file

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "app_version.h"
#include "memfault/components.h"
#include "memfault/ports/zephyr/core.h"
#include "memfault/ports/zephyr/http.h"

LOG_MODULE_REGISTER(mflt_app, LOG_LEVEL_DBG);

// This will be available in the memfault/ports/zephyr/core.h header in a later
// Memfault SDK release
extern const char* memfault_zephyr_get_device_id(void);

void memfault_platform_get_device_info(sMemfaultDeviceInfo* info) {
  // override the software version only to demonstrate inserting the GIT SHA
  *info = (sMemfaultDeviceInfo){
      .device_serial = memfault_zephyr_get_device_id(),
      .software_type = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_TYPE,
      .software_version = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_VERSION
      "+" ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1,
      .hardware_version = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_HARDWARE_VERSION,
  };
}

// Basic shell command to call memfault_zephyr_port_http_periodic_upload()
static int cmd_memfault_upload(const struct shell* shell, size_t argc,
                               char** argv) {
  // requires 1 arg, 1 or 0, to enable/disable
  if (argc == 2) {
    bool enable = strncmp(argv[1], "1", 2) == 0;
    memfault_zephyr_port_http_periodic_upload_enable(enable);
    shell_print(shell, "Periodic upload %s", enable ? "enabled" : "disabled");
    return 0;
  }

  shell_print(shell, "Usage: memfault_upload <1|0>");
  return 1;
}

SHELL_CMD_REGISTER(memfault_upload, NULL, "Trigger Memfault data upload",
                   cmd_memfault_upload);

int main(void) {
  LOG_INF("Memfault Demo App! Board %s\n", CONFIG_BOARD);

  printk("\n" MEMFAULT_BANNER_COLORIZED);

  memfault_device_info_dump();
  memfault_zephyr_port_install_root_certs();

  return 0;
}
